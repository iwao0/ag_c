#include "semantic_ctx.h"
#include "type_owned_internal.h"
#include "diag.h"
#include "type.h"
#include "type_builder.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define PCTX_HASH_BUCKETS 256

typedef struct goto_ref_t goto_ref_t;
struct goto_ref_t {
  goto_ref_t *next_all;
  char *name;
  int len;
  token_t *tok;
};

typedef struct label_def_t label_def_t;
struct label_def_t {
  label_def_t *next_hash;
  char *name;
  int len;
  token_t *tok;
};

typedef struct deferred_parser_warning_t deferred_parser_warning_t;
struct deferred_parser_warning_t {
  deferred_parser_warning_t *next_all;
  const token_t *tok;
  const char *name;
};

typedef struct tag_type_t tag_type_t;
struct tag_type_t {
  tag_type_t *next_hash;
  tag_type_t *next_all;
  token_kind_t kind;
  char *name;
  int len;
  int member_count;
  int is_complete;
  int size;
  int align;       // struct/union のアラインメント (_Alignof 用、agg_align)。0 = 未設定。
  int scope_depth;
  unsigned scope_seq;
  unsigned declaration_seq;
  psx_aggregate_definition_t *definition;
  tag_member_info_t *definition_members;
};
typedef struct tag_member_t tag_member_t;
struct tag_member_t {
  tag_member_t *next_hash;
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  char *member_name;
  int member_len;
  int offset;
  int bit_width;    // ビットフィールド幅（0: 非ビットフィールド）
  int bit_offset;   // ストレージユニット内ビット位置
  int bit_is_signed;
  psx_type_t *decl_type;
  int decl_order;
  int scope_depth;
};

typedef struct psx_ctx_allocation_t psx_ctx_allocation_t;
struct psx_ctx_allocation_t {
  psx_ctx_allocation_t *next;
  void *pointer;
};

static void *ctx_calloc_in(
    psx_semantic_context_t *context, size_t count, size_t size);
static void ctx_release_in(
    psx_semantic_context_t *context, void *pointer);
static void *ctx_calloc(size_t count, size_t size);
static void ctx_release(void *pointer);

static bool get_tag_member_info_impl(
    token_kind_t kind, char *name, int len, int scope_depth,
    int index, tag_member_info_t *out);

static void refresh_cached_tag_definition(tag_type_t *tag) {
  if (!tag || !tag->definition) return;
  psx_aggregate_definition_t *definition = tag->definition;
  tag_member_info_t *members = NULL;
  int member_count = tag->member_count;
  if (member_count > 0) {
    members = ctx_calloc((size_t)member_count, sizeof(tag_member_info_t));
    for (int i = 0; i < member_count; i++) {
      if (!get_tag_member_info_impl(
              tag->kind, tag->name, tag->len, tag->scope_depth,
              i, &members[i])) {
        member_count = i;
        break;
      }
    }
  }

  ctx_release(tag->definition_members);
  tag->definition_members = members;
  definition->tag_kind = tag->kind;
  definition->tag_name = tag->name;
  definition->tag_len = tag->len;
  definition->size = tag->size;
  definition->align = tag->align;
  definition->member_count = member_count;
  definition->members = tag->definition_members;
}

typedef struct enum_const_t enum_const_t;
struct enum_const_t {
  enum_const_t *next_hash;
  enum_const_t *next_all;
  char *name;
  int len;
  long long value;
  int scope_depth;
  unsigned scope_seq;
  unsigned declaration_seq;
};
typedef struct typedef_name_t typedef_name_t;
struct typedef_name_t {
  typedef_name_t *next_hash;
  typedef_name_t *next_all;
  char *name;
  int len;
  psx_type_t *decl_type;
  int scope_depth;
  unsigned scope_seq;
  unsigned declaration_seq;
};

static psx_type_t *tag_member_record_decl_type_mut(tag_member_t *m) {
  return m ? m->decl_type : NULL;
}

static const psx_type_t *typedef_record_decl_type(const typedef_name_t *t) {
  return t ? t->decl_type : NULL;
}

static psx_type_t *typedef_record_decl_type_mut(typedef_name_t *t) {
  return t ? t->decl_type : NULL;
}

struct psx_function_symbol_t {
  psx_function_symbol_t *next_hash;
  char *name;
  int len;
  const psx_type_t *function_type;
  /* 1: この関数名はすでに本体定義済み。2 度目の定義を E3064 で弾くために使う
   * (C11 6.9p3、`int f(){...} int f(){...}` 等)。プロトタイプ宣言 `int f(int);`
   * のみではこのフラグは立たない。 */
  int is_defined;
};

struct psx_semantic_context_t {
  psx_ctx_allocation_t *allocations;
  goto_ref_t *goto_refs_all;
  label_def_t *label_defs_by_bucket[PCTX_HASH_BUCKETS];
  deferred_parser_warning_t *deferred_parser_warnings_all;
  tag_type_t *tag_types_by_bucket[PCTX_HASH_BUCKETS];
  tag_type_t *all_tag_types;
  tag_member_t *tag_members_by_bucket[PCTX_HASH_BUCKETS];
  enum_const_t *enum_consts_by_bucket[PCTX_HASH_BUCKETS];
  enum_const_t *all_enum_consts;
  typedef_name_t *typedefs_by_bucket[PCTX_HASH_BUCKETS];
  typedef_name_t *all_typedefs;
  psx_function_symbol_t *function_symbols_by_bucket[PCTX_HASH_BUCKETS];
  int tag_scope_depth;
  int tag_member_decl_order;
};

static psx_semantic_context_t default_semantic_context;
static psx_semantic_context_t *active_semantic_context =
    &default_semantic_context;

static void *ctx_calloc_in(
    psx_semantic_context_t *context, size_t count, size_t size) {
  if (!context) return NULL;
  void *pointer = calloc(count, size);
  if (!pointer) return NULL;
  psx_ctx_allocation_t *allocation = malloc(sizeof(*allocation));
  if (!allocation) {
    free(pointer);
    return NULL;
  }
  allocation->pointer = pointer;
  allocation->next = context->allocations;
  context->allocations = allocation;
  return pointer;
}

static void ctx_release_in(
    psx_semantic_context_t *context, void *pointer) {
  if (!context || !pointer) return;
  psx_ctx_allocation_t **link = &context->allocations;
  while (*link && (*link)->pointer != pointer) link = &(*link)->next;
  if (!*link) return;
  psx_ctx_allocation_t *allocation = *link;
  *link = allocation->next;
  free(allocation->pointer);
  free(allocation);
}

static void *ctx_calloc(size_t count, size_t size) {
  return ctx_calloc_in(active_semantic_context, count, size);
}

static void ctx_release(void *pointer) {
  ctx_release_in(active_semantic_context, pointer);
}

static void ctx_release_all(psx_semantic_context_t *context) {
  if (!context) return;
  while (context->allocations) {
    psx_ctx_allocation_t *allocation = context->allocations;
    context->allocations = allocation->next;
    free(allocation->pointer);
    free(allocation);
  }
}

psx_semantic_context_t *ps_ctx_create(void) {
  return calloc(1, sizeof(psx_semantic_context_t));
}

void ps_ctx_destroy(psx_semantic_context_t *context) {
  if (!context || context == &default_semantic_context) return;
  if (active_semantic_context == context)
    active_semantic_context = &default_semantic_context;
  ctx_release_all(context);
  free(context);
}

psx_semantic_context_t *ps_ctx_activate(psx_semantic_context_t *context) {
  psx_semantic_context_t *previous = active_semantic_context;
  active_semantic_context = context ? context : &default_semantic_context;
  return previous;
}

psx_semantic_context_t *ps_ctx_active(void) {
  return active_semantic_context;
}

static psx_type_t *ctx_type_clone_persistent_in(
    psx_semantic_context_t *context, const psx_type_t *src) {
  if (!src) return NULL;
  psx_type_t *dst = ctx_calloc_in(context, 1, sizeof(*dst));
  if (!dst) return NULL;
  *dst = *src;
  dst->param_types = NULL;
  dst->base = ctx_type_clone_persistent_in(context, src->base);
  if (src->param_count > 0) {
    const psx_type_t **params =
        ctx_calloc_in(context, (size_t)src->param_count, sizeof(*params));
    if (!params) return NULL;
    for (int i = 0; i < src->param_count; i++)
      params[i] = ctx_type_clone_persistent_in(
          context,
          src->param_types ? src->param_types[i] : NULL);
    dst->param_types = params;
  }
  return dst;
}

static psx_type_t *ctx_type_clone_persistent(const psx_type_t *src) {
  return ctx_type_clone_persistent_in(active_semantic_context, src);
}

static void initialize_tag_member_record(tag_member_t *m,
                                         const tag_member_info_t *desc) {
  if (!m || !desc || m->decl_type) return;
  m->offset = desc->offset;
  m->bit_width = desc->bit_width;
  m->bit_offset = desc->bit_offset;
  m->bit_is_signed = desc->bit_is_signed;
  const psx_type_t *desc_type = ps_tag_member_decl_type(desc);
  m->decl_type = ctx_type_clone_persistent(desc_type);
}

#define goto_refs_all (active_semantic_context->goto_refs_all)
#define label_defs_by_bucket (active_semantic_context->label_defs_by_bucket)
#define deferred_parser_warnings_all \
  (active_semantic_context->deferred_parser_warnings_all)
#define tag_types_by_bucket (active_semantic_context->tag_types_by_bucket)
#define all_tag_types (active_semantic_context->all_tag_types)
#define tag_members_by_bucket (active_semantic_context->tag_members_by_bucket)
#define enum_consts_by_bucket (active_semantic_context->enum_consts_by_bucket)
#define all_enum_consts (active_semantic_context->all_enum_consts)
#define typedefs_by_bucket (active_semantic_context->typedefs_by_bucket)
#define all_typedefs (active_semantic_context->all_typedefs)
#define func_names_by_bucket \
  (active_semantic_context->function_symbols_by_bucket)
#define tag_scope_depth (active_semantic_context->tag_scope_depth)
#define tag_member_decl_order \
  (active_semantic_context->tag_member_decl_order)

static void refresh_registered_member_type_completeness(void) {
  for (int bucket = 0; bucket < PCTX_HASH_BUCKETS; bucket++) {
    for (tag_member_t *member = tag_members_by_bucket[bucket]; member;
         member = member->next_hash) {
      ps_ctx_refresh_type_completeness(
          tag_member_record_decl_type_mut(member));
    }
  }
}

static unsigned psx_ctx_hash_name(const char *name, int len) {
  // djb2 variant
  unsigned h = 5381u;
  for (int i = 0; i < len; i++) {
    h = ((h << 5) + h) ^ (unsigned char)name[i];
  }
  return h & (PCTX_HASH_BUCKETS - 1u);
}

static unsigned psx_ctx_hash_tag(token_kind_t kind, const char *name, int len) {
  unsigned h = (unsigned)kind * 131u;
  for (int i = 0; i < len; i++) {
    h = (h * 33u) ^ (unsigned char)name[i];
  }
  return h & (PCTX_HASH_BUCKETS - 1u);
}

/* 翻訳単位 (program) の境界で関数名テーブルを初期化する。
 * テストでは fork() 経由で複数のプログラムを 1 プロセス内で解析するため、
 * 関数戻り値型チェック等が前テストの登録に引きずられないようにする。 */
void ps_ctx_reset_function_names_in(psx_semantic_context_t *context) {
  if (!context) return;
  memset(context->function_symbols_by_bucket, 0,
         sizeof(context->function_symbols_by_bucket));
}

void ps_ctx_reset_function_names(void) {
  ps_ctx_reset_function_names_in(active_semantic_context);
}

void ps_ctx_reset_translation_unit_scope(void) {
  psx_semantic_context_t *context = active_semantic_context;
  ctx_release_all(context);
  memset(context, 0, sizeof(*context));
}

void ps_ctx_record_unsupported_gnu_extension_warning(const token_t *tok, const char *name) {
  deferred_parser_warning_t *w = ctx_calloc(1, sizeof(deferred_parser_warning_t));
  if (!w) {
    diag_emit_internalf(DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for(DIAG_ERR_INTERNAL_OOM));
  }
  w->tok = tok;
  w->name = name;
  w->next_all = deferred_parser_warnings_all;
  deferred_parser_warnings_all = w;
}

void ps_ctx_emit_deferred_parser_warnings(void) {
  deferred_parser_warning_t *rev = NULL;
  while (deferred_parser_warnings_all) {
    deferred_parser_warning_t *w = deferred_parser_warnings_all;
    deferred_parser_warnings_all = w->next_all;
    w->next_all = rev;
    rev = w;
  }
  while (rev) {
    deferred_parser_warning_t *w = rev;
    rev = w->next_all;
    diag_warn_tokf(DIAG_WARN_PARSER_UNSUPPORTED_GNU_EXTENSION, w->tok,
                   "%s: %s",
                   diag_warn_message_for(DIAG_WARN_PARSER_UNSUPPORTED_GNU_EXTENSION),
                   w->name ? w->name : "");
    ctx_release(w);
  }
}

/* タグの完全型定義状態をソフトリセットする。member tableも翻訳単位ごとの情報なので
 * 同時に破棄する。従来はmember recordを残して同名tagの次回parseで上書きしていたため、
 * duplicate判定を正しく行うと前回翻訳単位のmemberを誤検出していた。 */
void ps_ctx_reset_tag_diag_state(void) {
  for (unsigned i = 0; i < PCTX_HASH_BUCKETS; i++) {
    for (tag_type_t *t = tag_types_by_bucket[i]; t; t = t->next_hash) {
      t->member_count = 0;
      t->is_complete = 0;
      /* Published definitions remain valid for canonical types from the
       * previous parse. A later parse starts a new registry-owned generation. */
      t->definition = NULL;
      t->definition_members = NULL;
    }
  }
  memset(tag_members_by_bucket, 0, sizeof(tag_members_by_bucket));
  tag_member_decl_order = 0;
}

/* 各 parse 開始時に呼ぶ、関数名テーブルの「ソフトリセット」: 累積状態 (関数情報) は残し、
 * 同一 parse 内でのみ意味を持つ is_defined のみクリアする。これにより同一プロセス内で複数回frontend parseを
 * を呼ぶユニットテストで前回パースの "function defined" 状態が今回パースに漏れない。 */
void ps_ctx_reset_function_diag_state(void) {
  for (unsigned i = 0; i < PCTX_HASH_BUCKETS; i++) {
    for (psx_function_symbol_t *f = func_names_by_bucket[i];
         f; f = f->next_hash) {
      f->is_defined = 0;
    }
  }
}

void ps_ctx_reset_function_scope(void) {
  goto_refs_all = NULL;
  memset(label_defs_by_bucket, 0, sizeof(label_defs_by_bucket));
  tag_scope_depth = 0;
  tag_type_t **all_tag = &all_tag_types;
  while (*all_tag) {
    if ((*all_tag)->scope_depth > 0 || (*all_tag)->scope_seq != 0) {
      *all_tag = (*all_tag)->next_all;
      continue;
    }
    all_tag = &(*all_tag)->next_all;
  }
  typedef_name_t **all_typedef = &all_typedefs;
  while (*all_typedef) {
    if ((*all_typedef)->scope_depth > 0 ||
        (*all_typedef)->scope_seq != 0) {
      *all_typedef = (*all_typedef)->next_all;
      continue;
    }
    all_typedef = &(*all_typedef)->next_all;
  }
  enum_const_t **all_enum = &all_enum_consts;
  while (*all_enum) {
    if ((*all_enum)->scope_depth > 0 || (*all_enum)->scope_seq != 0) {
      *all_enum = (*all_enum)->next_all;
      continue;
    }
    all_enum = &(*all_enum)->next_all;
  }
  for (int i = 0; i < PCTX_HASH_BUCKETS; i++) {
    tag_type_t **tt = &tag_types_by_bucket[i];
    while (*tt) {
      if ((*tt)->scope_depth > 0) {
        *tt = (*tt)->next_hash;
        continue;
      }
      tt = &(*tt)->next_hash;
    }
    tag_member_t **tm = &tag_members_by_bucket[i];
    while (*tm) {
      if ((*tm)->scope_depth > 0) {
        *tm = (*tm)->next_hash;
        continue;
      }
      tm = &(*tm)->next_hash;
    }
    enum_const_t **ec = &enum_consts_by_bucket[i];
    while (*ec) {
      if ((*ec)->scope_depth > 0) {
        *ec = (*ec)->next_hash;
        continue;
      }
      ec = &(*ec)->next_hash;
    }
    typedef_name_t **td = &typedefs_by_bucket[i];
    while (*td) {
      if ((*td)->scope_depth > 0) {
        *td = (*td)->next_hash;
        continue;
      }
      td = &(*td)->next_hash;
    }
  }
}

void ps_ctx_enter_block_scope(void) {
  tag_scope_depth++;
}

void ps_ctx_leave_block_scope(void) {
  if (tag_scope_depth <= 0) return;
  int old_depth = tag_scope_depth;
  tag_scope_depth--;
  for (int i = 0; i < PCTX_HASH_BUCKETS; i++) {
    tag_type_t **pp = &tag_types_by_bucket[i];
    while (*pp) {
      tag_type_t *cur = *pp;
      if (cur->scope_depth >= old_depth) {
        *pp = cur->next_hash;
        continue;
      }
      pp = &cur->next_hash;
    }
  }
  for (int i = 0; i < PCTX_HASH_BUCKETS; i++) {
    typedef_name_t **pp = &typedefs_by_bucket[i];
    while (*pp) {
      typedef_name_t *cur = *pp;
      if (cur->scope_depth >= old_depth) {
        *pp = cur->next_hash;
        continue;
      }
      pp = &cur->next_hash;
    }
  }
  for (int i = 0; i < PCTX_HASH_BUCKETS; i++) {
    tag_member_t **pp = &tag_members_by_bucket[i];
    while (*pp) {
      tag_member_t *cur = *pp;
      if (cur->scope_depth >= old_depth) {
        *pp = cur->next_hash;
        continue;
      }
      pp = &cur->next_hash;
    }
  }
  for (int i = 0; i < PCTX_HASH_BUCKETS; i++) {
    enum_const_t **pp = &enum_consts_by_bucket[i];
    while (*pp) {
      enum_const_t *cur = *pp;
      if (cur->scope_depth >= old_depth) {
        *pp = cur->next_hash;
        continue;
      }
      pp = &cur->next_hash;
    }
  }
}

void psx_ctx_register_goto_ref(char *name, int len, token_t *tok) {
  goto_ref_t *g = ctx_calloc(1, sizeof(goto_ref_t));
  g->name = name;
  g->len = len;
  g->tok = tok;
  g->next_all = goto_refs_all;
  goto_refs_all = g;
}

void psx_ctx_register_label_def(char *name, int len, token_t *tok) {
  unsigned bucket = psx_ctx_hash_name(name, len);
  for (label_def_t *d = label_defs_by_bucket[bucket]; d; d = d->next_hash) {
    if (d->len == len && strncmp(d->name, name, (size_t)len) == 0) {
      ps_diag_duplicate_with_name(tok, diag_text_for(DIAG_TEXT_LABEL), name, len);
    }
  }
  label_def_t *d = ctx_calloc(1, sizeof(label_def_t));
  d->name = name;
  d->len = len;
  d->tok = tok;
  d->next_hash = label_defs_by_bucket[bucket];
  label_defs_by_bucket[bucket] = d;
}

void psx_ctx_validate_goto_refs(void) {
  for (goto_ref_t *g = goto_refs_all; g; g = g->next_all) {
    unsigned bucket = psx_ctx_hash_name(g->name, g->len);
    int found = 0;
    for (label_def_t *d = label_defs_by_bucket[bucket]; d; d = d->next_hash) {
      if (d->len == g->len && strncmp(d->name, g->name, (size_t)g->len) == 0) {
        found = 1;
        break;
      }
    }
    if (!found) {
      ps_diag_ctx(g->tok, "goto", diag_message_for(DIAG_ERR_PARSER_GOTO_LABEL_UNDEFINED),
                   g->len, g->name);
    }
  }
}

// tag_types_by_bucket から (kind, name, len) に一致するエントリを返す。なければ NULL。
static tag_type_t *find_tag_type(token_kind_t kind, char *name, int len) {
  unsigned bucket = psx_ctx_hash_tag(kind, name, len);
  for (tag_type_t *t = tag_types_by_bucket[bucket]; t; t = t->next_hash) {
    if (t->kind == kind && t->len == len && strncmp(t->name, name, (size_t)len) == 0) {
      return t;
    }
  }
  return NULL;
}

bool ps_ctx_has_tag_type(token_kind_t kind, char *name, int len) {
  return find_tag_type(kind, name, len) != NULL;
}

psx_type_t *ps_ctx_clone_tag_type_at(
    token_kind_t kind, char *name, int len,
    psx_local_lookup_point_t point) {
  for (tag_type_t *tag = all_tag_types; tag; tag = tag->next_all) {
    if (tag->kind != kind || tag->len != len ||
        strncmp(tag->name, name, (size_t)len) != 0 ||
        (tag->scope_depth > 0 &&
         tag->declaration_seq > point.declaration_seq) ||
        !ps_local_registry_scope_is_visible_from(
            tag->scope_seq, point.scope_seq))
      continue;
    psx_type_t *type = kind == TK_ENUM
        ? ps_type_new_enum(
              name, len, tag->scope_depth + 1,
              tag->size > 0 ? tag->size : 4)
        : ps_type_new_tag(
              kind, name, len, tag->scope_depth + 1, tag->size);
    type->aggregate_definition = tag->definition;
    if (tag->align > 0) type->align = tag->align;
    return type;
  }
  return NULL;
}

void psx_ctx_define_tag_type(token_kind_t kind, char *name, int len) {
  psx_ctx_define_tag_type_with_layout(kind, name, len, 0, 0, 0);
}

void psx_ctx_define_tag_type_with_members(token_kind_t kind, char *name, int len, int member_count) {
  psx_ctx_define_tag_type_with_layout(kind, name, len, member_count, member_count > 0 ? 8 : 0, 0);
}

void psx_ctx_define_tag_type_with_layout(token_kind_t kind, char *name, int len,
                                         int member_count, int tag_size, int tag_align) {
  int is_complete = member_count > 0 || tag_size > 0 || tag_align > 0;
  if (!ps_ctx_register_tag_type(kind, name, len, is_complete,
                                 member_count, tag_size, tag_align)) {
    diag_emit_tokf(DIAG_ERR_PARSER_INVALID_CONTEXT, NULL,
                   "タグ '%.*s' は同一スコープで再定義されています (C11 6.7.2)",
                   len, name);
  }
}

int ps_ctx_register_tag_type(token_kind_t kind, char *name, int len,
                              int is_complete, int member_count,
                              int tag_size, int tag_align) {
  if (!name || len <= 0) return 0;
  tag_type_t *existing = find_tag_type(kind, name, len);
  /* 同じスコープでの再宣言 (前方宣言 `struct S;` → 定義 `struct S{...}`) のみ既存を update する。
   * 内側スコープに同名タグを別レイアウトで宣言した場合 (`struct S{int a;}` 外側 → ブロック内
   * `struct S{double x;}`) は新規エントリとして先頭挿入し、leave_block_scope で削除されるよう
   * scope_depth を立てる。find_tag_type は先頭から最初の一致を返すので、内側 shadow が優先される。 */
  if (existing && existing->scope_depth == tag_scope_depth) {
    /* C11 6.7.2.1p1 / 6.7.2.2p2 / 6.7.2.3p3: 同一スコープでの完全型タグの再定義は不可。
     * 既存もメンバを持っている (= 完全型) のに、今回も新しいメンバを持っている (= 完全型) なら
     * 二重定義。一方が前方宣言なら従来どおり update。 */
    if (existing->is_complete && is_complete) return 0;
    if (member_count > existing->member_count) existing->member_count = member_count;
    if (tag_size > existing->size) existing->size = tag_size;
    if (tag_align > existing->align) existing->align = tag_align;
    if (is_complete) existing->is_complete = 1;
    if (is_complete) refresh_registered_member_type_completeness();
    if (existing->is_complete && !existing->definition)
      (void)ps_ctx_get_tag_definition(kind, name, len);
    else
      refresh_cached_tag_definition(existing);
    return 1;
  }
  unsigned bucket = psx_ctx_hash_tag(kind, name, len);
  tag_type_t *t = ctx_calloc(1, sizeof(tag_type_t));
  t->kind = kind;
  t->name = name;
  t->len = len;
  t->member_count = member_count;
  t->is_complete = is_complete ? 1 : 0;
  t->size = tag_size;
  t->align = tag_align;
  t->scope_depth = tag_scope_depth;
  t->scope_seq = ps_local_registry_current_scope_seq();
  t->declaration_seq = ps_local_registry_register_binding_event();
  t->next_hash = tag_types_by_bucket[bucket];
  tag_types_by_bucket[bucket] = t;
  t->next_all = all_tag_types;
  all_tag_types = t;
  if (t->is_complete) {
    refresh_registered_member_type_completeness();
    (void)ps_ctx_get_tag_definition(kind, name, len);
  }
  return 1;
}

int ps_ctx_current_tag_scope_depth(void) {
  return tag_scope_depth;
}

int ps_ctx_find_tag_kind_at_current_scope(
    char *name, int len, token_kind_t *out_kind) {
  if (!name || len <= 0) return 0;
  for (unsigned i = 0; i < PCTX_HASH_BUCKETS; i++) {
    for (tag_type_t *tag = tag_types_by_bucket[i]; tag;
         tag = tag->next_hash) {
      if (tag->scope_depth != tag_scope_depth || tag->len != len ||
          strncmp(tag->name, name, (size_t)len) != 0) {
        continue;
      }
      if (out_kind) *out_kind = tag->kind;
      return 1;
    }
  }
  return 0;
}

int ps_ctx_get_tag_member_count(token_kind_t kind, char *name, int len) {
  tag_type_t *t = find_tag_type(kind, name, len);
  return t ? t->member_count : -1;
}

const psx_aggregate_definition_t *ps_ctx_get_tag_definition(
    token_kind_t kind, char *name, int len) {
  tag_type_t *tag = find_tag_type(kind, name, len);
  if (!tag) return NULL;
  if (tag->definition) return tag->definition;

  psx_aggregate_definition_t *definition =
      ctx_calloc(1, sizeof(psx_aggregate_definition_t));
  tag->definition = definition;
  refresh_cached_tag_definition(tag);
  return definition;
}

int ps_ctx_get_tag_size(token_kind_t kind, char *name, int len) {
  tag_type_t *t = find_tag_type(kind, name, len);
  return t ? t->size : -1;
}

int ps_ctx_get_tag_align(token_kind_t kind, char *name, int len) {
  tag_type_t *t = find_tag_type(kind, name, len);
  return (t && t->align > 0) ? t->align : -1;
}

static tag_member_t *find_tag_member_record_at_current_scope(
    token_kind_t tag_kind, char *tag_name, int tag_len,
    const tag_member_info_t *desc, unsigned bucket) {
  if (!desc || desc->len <= 0) return NULL;
  for (tag_member_t *m = tag_members_by_bucket[bucket]; m; m = m->next_hash) {
    if (m->tag_kind == tag_kind && m->tag_len == tag_len &&
        m->member_len == desc->len &&
        strncmp(m->tag_name, tag_name, (size_t)tag_len) == 0 &&
        strncmp(m->member_name, desc->name, (size_t)desc->len) == 0 &&
        m->scope_depth == tag_scope_depth) {
      return m;
    }
  }
  return NULL;
}

static void insert_tag_member_record(
    token_kind_t tag_kind, char *tag_name, int tag_len,
    const tag_member_info_t *desc, unsigned bucket) {
  tag_member_t *m = ctx_calloc(1, sizeof(tag_member_t));
  m->tag_kind = tag_kind;
  m->tag_name = tag_name;
  m->tag_len = tag_len;
  m->member_name = desc->name;
  m->member_len = desc->len;
  initialize_tag_member_record(m, desc);
  m->decl_order = tag_member_decl_order++;
  m->scope_depth = tag_scope_depth;
  m->next_hash = tag_members_by_bucket[bucket];
  tag_members_by_bucket[bucket] = m;
}

int psx_ctx_register_tag_member(
    token_kind_t tag_kind, char *tag_name, int tag_len,
    const tag_member_info_t *desc, int *out_created) {
  if (out_created) *out_created = 0;
  if (!ps_ctx_register_tag_members(
          tag_kind, tag_name, tag_len, desc, 1, NULL))
    return 0;
  if (out_created) *out_created = 1;
  return 1;
}

int ps_ctx_register_tag_members(
    token_kind_t tag_kind, char *tag_name, int tag_len,
    const tag_member_info_t *members, int member_count,
    int *out_conflict_index) {
  if (out_conflict_index) *out_conflict_index = -1;
  if ((tag_kind != TK_STRUCT && tag_kind != TK_UNION) || !tag_name ||
      tag_len <= 0 || !members || member_count <= 0) {
    return 0;
  }

  for (int i = 0; i < member_count; i++) {
    const tag_member_info_t *desc = &members[i];
    if (!desc->name || desc->len < 0 || !ps_tag_member_decl_type(desc)) {
      if (out_conflict_index) *out_conflict_index = i;
      return 0;
    }
    if (desc->len == 0) continue;
    unsigned bucket = (psx_ctx_hash_tag(tag_kind, tag_name, tag_len) ^
                       psx_ctx_hash_name(desc->name, desc->len)) &
                      (PCTX_HASH_BUCKETS - 1u);
    if (find_tag_member_record_at_current_scope(
            tag_kind, tag_name, tag_len, desc, bucket)) {
      if (out_conflict_index) *out_conflict_index = i;
      return 0;
    }
    for (int j = 0; j < i; j++) {
      if (members[j].len == desc->len && desc->len > 0 &&
          strncmp(members[j].name, desc->name, (size_t)desc->len) == 0) {
        if (out_conflict_index) *out_conflict_index = i;
        return 0;
      }
    }
  }

  for (int i = 0; i < member_count; i++) {
    const tag_member_info_t *desc = &members[i];
    unsigned bucket = (psx_ctx_hash_tag(tag_kind, tag_name, tag_len) ^
                       psx_ctx_hash_name(desc->name, desc->len)) &
                      (PCTX_HASH_BUCKETS - 1u);
    insert_tag_member_record(tag_kind, tag_name, tag_len, desc, bucket);
  }
  tag_type_t *tag = find_tag_type(tag_kind, tag_name, tag_len);
  if (tag && tag->definition) refresh_cached_tag_definition(tag);
  return 1;
}

static int cmp_tag_member_ptr(const void *a, const void *b) {
  const tag_member_t *ma = *(const tag_member_t * const *)a;
  const tag_member_t *mb = *(const tag_member_t * const *)b;
  if (ma->offset != mb->offset) return (ma->offset < mb->offset) ? -1 : 1;
  if (ma->decl_order != mb->decl_order) return (ma->decl_order < mb->decl_order) ? -1 : 1;
  return 0;
}

/* tag_member_t の全属性を tag_member_info_t へ写す。get/find_tag_member_info が
 * メンバを 1 つ特定したあとに使う (旧実装の複数 getter 呼び分けを 1 箇所に集約)。 */
void ps_ctx_attach_aggregate_definitions(psx_type_t *type) {
  if (!type) return;
  if (ps_type_is_tag_aggregate(type) && !type->aggregate_definition) {
    type->aggregate_definition = ps_ctx_get_tag_definition(
        type->tag_kind, type->tag_name, type->tag_len);
  }
  if (type->kind == PSX_TYPE_POINTER || type->kind == PSX_TYPE_ARRAY ||
      type->kind == PSX_TYPE_FUNCTION) {
    ps_ctx_attach_aggregate_definitions(psx_type_owned_base_mut(type));
  }
}

static void fill_tag_member_info(const tag_member_t *m, tag_member_info_t *out) {
  memset(out, 0, sizeof(*out));
  out->name = m->member_name;
  out->len = m->member_len;
  out->offset = m->offset;
  out->bit_width = m->bit_width;
  out->bit_offset = m->bit_offset;
  out->bit_is_signed = m->bit_is_signed;
  psx_type_t *decl_type = tag_member_record_decl_type_mut((tag_member_t *)m);
  ps_ctx_refresh_type_completeness(decl_type);
  ps_ctx_attach_aggregate_definitions(decl_type);
  out->decl_type = decl_type;
}

/* 内部実装: scope_depth が指定 (>=0) ならその深度に固定、負なら find_tag_type の
 * 最も内側 tag の scope_depth を使う。 */
static bool get_tag_member_info_impl(token_kind_t kind, char *name, int len,
                                     int scope_depth, int index, tag_member_info_t *out) {
  if (!out) return false;
  int target_scope = scope_depth;
  if (target_scope < 0) {
    tag_type_t *tt = find_tag_type(kind, name, len);
    if (!tt) return false;
    target_scope = tt->scope_depth;
  }
  int cap = 8;
  int n = 0;
  tag_member_t **members = calloc((size_t)cap, sizeof(tag_member_t *));
  for (int i = 0; i < PCTX_HASH_BUCKETS; i++) {
    for (tag_member_t *m = tag_members_by_bucket[i]; m; m = m->next_hash) {
      if (m->tag_kind != kind || m->tag_len != len) continue;
      if (strncmp(m->tag_name, name, (size_t)len) != 0) continue;
      if (m->scope_depth != target_scope) continue;
      if (n >= cap) {
        cap *= 2;
        members = realloc(members, (size_t)cap * sizeof(tag_member_t *));
      }
      members[n++] = m;
    }
  }
  if (n == 0 || index < 0 || index >= n) {
    free(members);
    return false;
  }
  qsort(members, (size_t)n, sizeof(tag_member_t *), cmp_tag_member_ptr);
  fill_tag_member_info(members[index], out);
  free(members);
  return true;
}

static bool find_tag_member_info_impl(token_kind_t kind, char *name, int len,
                                      int scope_depth,
                                      char *member_name, int member_len, tag_member_info_t *out) {
  if (!out) return false;
  int target_scope = scope_depth;
  if (target_scope < 0) {
    tag_type_t *tt = find_tag_type(kind, name, len);
    if (!tt) return false;
    target_scope = tt->scope_depth;
  }
  unsigned bucket = (psx_ctx_hash_tag(kind, name, len) ^
                     psx_ctx_hash_name(member_name, member_len)) & (PCTX_HASH_BUCKETS - 1u);
  for (tag_member_t *m = tag_members_by_bucket[bucket]; m; m = m->next_hash) {
    if (m->tag_kind == kind && m->tag_len == len &&
        m->member_len == member_len &&
        strncmp(m->tag_name, name, (size_t)len) == 0 &&
        strncmp(m->member_name, member_name, (size_t)member_len) == 0 &&
        m->scope_depth == target_scope) {
      fill_tag_member_info(m, out);
      return true;
    }
  }
  return false;
}

/* tag の index 番目 (offset 昇順) のメンバ全属性を取得する。最も内側 tag の scope_depth に
 * 固定 (shadow 対応)。 */
bool ps_ctx_get_tag_member_info(token_kind_t kind, char *name, int len, int index,
                                  tag_member_info_t *out) {
  return get_tag_member_info_impl(kind, name, len, -1, index, out);
}

/* 名前検索版の統合 API。 */
bool ps_ctx_find_tag_member_info(token_kind_t kind, char *name, int len,
                                   char *member_name, int member_len,
                                   tag_member_info_t *out) {
  return find_tag_member_info_impl(kind, name, len, -1, member_name, member_len, out);
}

/* 特定 scope_depth に固定した版。タグ shadowing の応用形で、変数の宣言時 scope を引数で
 * 指定してその scope のメンバを引くのに使う。 */
bool ps_ctx_get_tag_member_info_at_scope(token_kind_t kind, char *name, int len,
                                          int scope_depth, int index,
                                          tag_member_info_t *out) {
  return get_tag_member_info_impl(kind, name, len, scope_depth, index, out);
}

bool ps_ctx_find_tag_member_info_at_scope(token_kind_t kind, char *name, int len,
                                           int scope_depth,
                                           char *member_name, int member_len,
                                           tag_member_info_t *out) {
  return find_tag_member_info_impl(kind, name, len, scope_depth, member_name, member_len, out);
}

int ps_ctx_get_tag_scope_depth(token_kind_t kind, char *name, int len) {
  tag_type_t *t = find_tag_type(kind, name, len);
  return t ? t->scope_depth : -1;
}

void ps_ctx_promote_tag_to_file_scope(token_kind_t kind, char *name, int len) {
  tag_type_t *t = find_tag_type(kind, name, len);
  if (!t || t->scope_depth == 0) return;
  int old_depth = t->scope_depth;
  t->scope_depth = 0;
  for (int i = 0; i < PCTX_HASH_BUCKETS; i++) {
    for (tag_member_t *m = tag_members_by_bucket[i]; m; m = m->next_hash) {
      if (m->tag_kind == kind && m->tag_len == len &&
          m->scope_depth == old_depth &&
          strncmp(m->tag_name, name, (size_t)len) == 0) {
        m->scope_depth = 0;
      }
    }
  }
}

int ps_ctx_get_tag_member_count_at_scope(token_kind_t kind, char *name, int len, int scope_depth) {
  /* 該当スコープの tag を線形検索 (find_tag_type は最も内側を返すので使えない)。 */
  unsigned bucket = psx_ctx_hash_tag(kind, name, len);
  for (tag_type_t *t = tag_types_by_bucket[bucket]; t; t = t->next_hash) {
    if (t->kind == kind && t->len == len &&
        t->scope_depth == scope_depth &&
        strncmp(t->name, name, (size_t)len) == 0) {
      return t->member_count;
    }
  }
  return -1;
}

// 任意のスコープから名前一致の enum_const を返す。なければ NULL。
static enum_const_t *find_enum_const(char *name, int len) {
  unsigned bucket = psx_ctx_hash_name(name, len);
  for (enum_const_t *e = enum_consts_by_bucket[bucket]; e; e = e->next_hash) {
    if (e->len == len && strncmp(e->name, name, (size_t)len) == 0) {
      return e;
    }
  }
  return NULL;
}

// 現スコープ深度に限った検索（同名再定義の検出用）。
static enum_const_t *find_enum_const_in_current_scope(char *name, int len) {
  unsigned bucket = psx_ctx_hash_name(name, len);
  for (enum_const_t *e = enum_consts_by_bucket[bucket]; e; e = e->next_hash) {
    if (e->scope_depth == tag_scope_depth && e->len == len &&
        strncmp(e->name, name, (size_t)len) == 0) {
      return e;
    }
  }
  return NULL;
}

/* enum 定数を登録する。
 * 戻り値: 1 = 新規登録に成功、0 = 同名定数が既に同スコープにあった (重複)。
 * 重複時はテーブルを変更しない (呼び出し元で診断を出す)。 */
int ps_ctx_register_enum_const(
    char *name, int len, long long value, int *out_created) {
  if (out_created) *out_created = 0;
  enum_const_t *existing = find_enum_const_in_current_scope(name, len);
  if (existing) {
    return 0;
  }
  unsigned bucket = psx_ctx_hash_name(name, len);
  enum_const_t *e = ctx_calloc(1, sizeof(enum_const_t));
  e->name = name;
  e->len = len;
  e->value = value;
  e->scope_depth = tag_scope_depth;
  e->scope_seq = ps_local_registry_current_scope_seq();
  e->declaration_seq = ps_local_registry_register_binding_event();
  e->next_hash = enum_consts_by_bucket[bucket];
  enum_consts_by_bucket[bucket] = e;
  e->next_all = all_enum_consts;
  all_enum_consts = e;
  if (out_created) *out_created = 1;
  return 1;
}

int psx_ctx_define_enum_const(char *name, int len, long long value) {
  return ps_ctx_register_enum_const(name, len, value, NULL);
}

bool ps_ctx_find_enum_const(char *name, int len, long long *out_value) {
  enum_const_t *e = find_enum_const(name, len);
  if (!e) return false;
  if (out_value) *out_value = e->value;
  return true;
}

bool ps_ctx_find_enum_const_at(
    char *name, int len, psx_local_lookup_point_t point,
    long long *out_value) {
  if (!name || len <= 0) return false;
  for (enum_const_t *e = all_enum_consts; e; e = e->next_all) {
    if (e->len != len ||
        strncmp(e->name, name, (size_t)len) != 0 ||
        (e->scope_seq != 0 &&
         e->declaration_seq > point.declaration_seq) ||
        !ps_local_registry_scope_is_visible_from(
            e->scope_seq, point.scope_seq))
      continue;
    if (out_value) *out_value = e->value;
    return true;
  }
  return false;
}

int ps_ctx_has_enum_const_in_current_scope(char *name, int len) {
  return find_enum_const_in_current_scope(name, len) != NULL;
}

// 任意のスコープから名前一致の typedef を返す。なければ NULL。
static typedef_name_t *find_typedef(char *name, int len) {
  unsigned bucket = psx_ctx_hash_name(name, len);
  for (typedef_name_t *t = typedefs_by_bucket[bucket]; t; t = t->next_hash) {
    if (t->len == len && strncmp(t->name, name, (size_t)len) == 0) {
      return t;
    }
  }
  return NULL;
}

// 現スコープ深度に限った検索（同名再定義の検出用）。
static typedef_name_t *find_typedef_in_current_scope(char *name, int len) {
  unsigned bucket = psx_ctx_hash_name(name, len);
  for (typedef_name_t *t = typedefs_by_bucket[bucket]; t; t = t->next_hash) {
    if (t->scope_depth == tag_scope_depth && t->len == len &&
        strncmp(t->name, name, (size_t)len) == 0) {
      return t;
    }
  }
  return NULL;
}

int ps_ctx_has_typedef_in_current_scope(char *name, int len) {
  return find_typedef_in_current_scope(name, len) != NULL;
}

void ps_ctx_refresh_type_completeness(psx_type_t *type) {
  if (!type) return;
  if (type->kind == PSX_TYPE_STRUCT || type->kind == PSX_TYPE_UNION) {
    int size = ps_ctx_get_tag_size(type->tag_kind, type->tag_name,
                                    type->tag_len);
    if (size > 0) {
      type->size = size;
      if (type->align <= 0) type->align = size >= 8 ? 8 : size;
    }
  }
  ps_ctx_refresh_type_completeness(psx_type_owned_base_mut(type));
  if (type->kind == PSX_TYPE_ARRAY && type->base) {
    int element_size = ps_type_sizeof(type->base);
    if (element_size > 0) {
      if (type->array_len > 0 && type->array_len <= INT_MAX / element_size)
        type->size = type->array_len * element_size;
    }
  }
  if (type->kind == PSX_TYPE_FUNCTION) {
    for (int i = 0; i < type->param_count; i++)
      ps_ctx_refresh_type_completeness(
          psx_type_owned_param_mut(type, i));
  }
}

static void initialize_typedef_record(
    typedef_name_t *t, const psx_typedef_info_t *info) {
  if (!t || !info || t->decl_type) return;
  t->decl_type = ctx_type_clone_persistent(ps_ctx_typedef_decl_type(info));
}

int ps_ctx_register_typedef_name(
    char *name, int len, const psx_typedef_info_t *info,
    int *out_created, int *out_redeclared) {
  if (out_created) *out_created = 0;
  if (out_redeclared) *out_redeclared = 0;
  if (!info || !ps_ctx_typedef_decl_type(info)) return 0;
  typedef_name_t *existing = find_typedef_in_current_scope(name, len);
  /* C11 6.7p3: typedef は同じ型なら再宣言可。違う型なら error。
   * 比較するフィールドは「型の identity」を成すもの。tag_name は同じ ptr で
   * あるはずなので ptr 比較で十分 (parser が tag を共有させている)。 */
  if (existing) {
    const psx_type_t *new_decl_type = ps_ctx_typedef_decl_type(info);
    const psx_type_t *existing_decl_type = typedef_record_decl_type(existing);
    if (!ps_type_shape_matches(existing_decl_type, new_decl_type)) return 0;
    if (out_redeclared) *out_redeclared = 1;
    return 1;  /* 同じ型なら登録済みのままで OK */
  }
  unsigned bucket = psx_ctx_hash_name(name, len);
  typedef_name_t *t = ctx_calloc(1, sizeof(typedef_name_t));
  t->name = name;
  t->len = len;
  t->scope_depth = tag_scope_depth;
  t->scope_seq = ps_local_registry_current_scope_seq();
  t->declaration_seq = ps_local_registry_register_binding_event();
  t->next_hash = typedefs_by_bucket[bucket];
  typedefs_by_bucket[bucket] = t;
  t->next_all = all_typedefs;
  all_typedefs = t;
  initialize_typedef_record(t, info);
  if (out_created) *out_created = 1;
  return 1;
}

int psx_ctx_define_typedef_name(
    char *name, int len, const psx_typedef_info_t *info) {
  return ps_ctx_register_typedef_name(name, len, info, NULL, NULL);
}

bool psx_ctx_find_typedef_sizeof(char *name, int len, int *out_sizeof_size) {
  typedef_name_t *t = find_typedef(name, len);
  if (!t) return false;
  ps_ctx_refresh_type_completeness(typedef_record_decl_type_mut(t));
  if (out_sizeof_size)
    *out_sizeof_size = ps_type_sizeof(typedef_record_decl_type(t));
  return true;
}

bool ps_ctx_find_typedef_name(char *name, int len, psx_typedef_info_t *out) {
  typedef_name_t *t = find_typedef(name, len);
  if (!t) return false;
  ps_ctx_refresh_type_completeness(typedef_record_decl_type_mut(t));
  if (out) {
    memset(out, 0, sizeof(*out));
    out->decl_type = typedef_record_decl_type(t);
  }
  return true;
}

bool ps_ctx_find_typedef_decl_type(
    char *name, int len, const psx_type_t **out_type) {
  typedef_name_t *t = find_typedef(name, len);
  if (!t) return false;
  ps_ctx_refresh_type_completeness(typedef_record_decl_type_mut(t));
  if (out_type) *out_type = typedef_record_decl_type(t);
  return true;
}

bool ps_ctx_find_typedef_decl_type_at(
    char *name, int len, psx_local_lookup_point_t point,
    const psx_type_t **out_type) {
  for (typedef_name_t *type = all_typedefs; type;
       type = type->next_all) {
    if (type->len != len ||
        strncmp(type->name, name, (size_t)len) != 0 ||
        (type->scope_depth > 0 &&
         type->declaration_seq > point.declaration_seq) ||
        !ps_local_registry_scope_is_visible_from(
            type->scope_seq, point.scope_seq))
      continue;
    ps_ctx_refresh_type_completeness(
        typedef_record_decl_type_mut(type));
    if (out_type) *out_type = typedef_record_decl_type(type);
    return true;
  }
  return false;
}

bool psx_ctx_is_typedef_name_token(token_t *tok) {
  if (!tok || tok->kind != TK_IDENT) return false;
  token_ident_t *id = (token_ident_t *)tok;
  return ps_ctx_find_typedef_name(id->str, id->len, NULL);
}

void psx_ctx_define_function_name(char *name, int len) {
  psx_ctx_define_function_name_with_ret(name, len, 0);
}

// 任意のスコープから名前一致の関数名エントリを返す。なければ NULL。
const psx_function_symbol_t *ps_ctx_find_function_symbol_in(
    psx_semantic_context_t *context, char *name, int len) {
  if (!context || !name || len <= 0) return NULL;
  unsigned bucket = psx_ctx_hash_name(name, len);
  for (psx_function_symbol_t *f =
           context->function_symbols_by_bucket[bucket];
       f; f = f->next_hash) {
    if (f->len == len && strncmp(f->name, name, (size_t)len) == 0) {
      return f;
    }
  }
  return NULL;
}

const psx_function_symbol_t *ps_ctx_find_function_symbol(
    char *name, int len) {
  return ps_ctx_find_function_symbol_in(
      active_semantic_context, name, len);
}

static psx_function_symbol_t *find_function_name_mut_in(
    psx_semantic_context_t *context, char *name, int len) {
  return (psx_function_symbol_t *)ps_ctx_find_function_symbol_in(
      context, name, len);
}

static psx_function_symbol_t *find_function_name_mut(
    char *name, int len) {
  return find_function_name_mut_in(active_semantic_context, name, len);
}

const psx_type_t *ps_function_symbol_type(
    const psx_function_symbol_t *symbol) {
  return symbol ? symbol->function_type : NULL;
}

void ps_ctx_checkpoint_function_registration_in(
    psx_semantic_context_t *context, char *name, int len,
    psx_function_registration_checkpoint_t *checkpoint) {
  if (!checkpoint) return;
  *checkpoint = (psx_function_registration_checkpoint_t){0};
  if (!context || !name || len <= 0) return;
  psx_function_symbol_t *function =
      find_function_name_mut_in(context, name, len);
  if (!function) return;
  checkpoint->existed = 1;
  checkpoint->is_defined = function->is_defined;
  checkpoint->function_type = function->function_type;
}

void ps_ctx_checkpoint_function_registration(
    char *name, int len, psx_function_registration_checkpoint_t *checkpoint) {
  ps_ctx_checkpoint_function_registration_in(
      active_semantic_context, name, len, checkpoint);
}

void ps_ctx_rollback_function_registration_in(
    psx_semantic_context_t *context, char *name, int len,
    const psx_function_registration_checkpoint_t *checkpoint) {
  if (!context || !checkpoint || !name || len <= 0) return;
  unsigned bucket = psx_ctx_hash_name(name, len);
  psx_function_symbol_t **link =
      &context->function_symbols_by_bucket[bucket];
  while (*link && ((*link)->len != len ||
                   strncmp((*link)->name, name, (size_t)len) != 0)) {
    link = &(*link)->next_hash;
  }
  if (!*link) return;
  if (!checkpoint->existed) {
    psx_function_symbol_t *removed = *link;
    *link = removed->next_hash;
    ctx_release_in(context, removed);
    return;
  }
  (*link)->is_defined = checkpoint->is_defined;
  (*link)->function_type = checkpoint->function_type;
}

void ps_ctx_rollback_function_registration(
    char *name, int len,
    const psx_function_registration_checkpoint_t *checkpoint) {
  ps_ctx_rollback_function_registration_in(
      active_semantic_context, name, len, checkpoint);
}

static void define_function_name_with_ret_in(
    psx_semantic_context_t *context, char *name, int len,
    int ret_struct_size) {
  if (!context || !name || len <= 0) return;
  psx_function_symbol_t *existing =
      find_function_name_mut_in(context, name, len);
  if (existing) return;
  unsigned bucket = psx_ctx_hash_name(name, len);
  psx_function_symbol_t *f =
      ctx_calloc_in(context, 1, sizeof(*f));
  if (!f) return;
  f->name = name;
  f->len = len;
  (void)ret_struct_size;
  f->next_hash = context->function_symbols_by_bucket[bucket];
  context->function_symbols_by_bucket[bucket] = f;
}

void psx_ctx_define_function_name_with_ret(char *name, int len,
                                           int ret_struct_size) {
  define_function_name_with_ret_in(
      active_semantic_context, name, len, ret_struct_size);
}

bool ps_ctx_has_function_name_in(
    psx_semantic_context_t *context, char *name, int len) {
  return ps_ctx_find_function_symbol_in(context, name, len) != NULL;
}

bool ps_ctx_has_function_name(char *name, int len) {
  return ps_ctx_has_function_name_in(active_semantic_context, name, len);
}

/* 同名関数の本体定義が初回かどうかをチェック・記録する (C11 6.9p3)。
 * 初回 (まだ立っていない) なら 1 を返してフラグを立てる、すでに定義済みなら 0 を返す。 */
int ps_ctx_track_function_defined_in(
    psx_semantic_context_t *context, char *name, int len) {
  psx_function_symbol_t *f =
      find_function_name_mut_in(context, name, len);
  if (!f) return 1;
  if (f->is_defined) return 0;
  f->is_defined = 1;
  return 1;
}

int ps_ctx_track_function_defined(char *name, int len) {
  return ps_ctx_track_function_defined_in(
      active_semantic_context, name, len);
}

int ps_ctx_is_function_defined(char *name, int len) {
  psx_function_symbol_t *f = find_function_name_mut(name, len);
  return f && f->is_defined;
}

const psx_type_t *psx_ctx_get_function_ret_type(char *name, int len) {
  const psx_function_symbol_t *f =
      ps_ctx_find_function_symbol(name, len);
  return f ? ps_type_function_return_type(f->function_type) : NULL;
}

const psx_function_symbol_t *ps_ctx_register_function_type_in(
    psx_semantic_context_t *context, char *name, int len,
    const psx_type_t *function_type) {
  if (!context || !name || len <= 0 || !function_type ||
      function_type->kind != PSX_TYPE_FUNCTION) {
    return NULL;
  }
  psx_function_symbol_t *f =
      find_function_name_mut_in(context, name, len);
  if (!f) {
    define_function_name_with_ret_in(context, name, len, 0);
    f = find_function_name_mut_in(context, name, len);
  }
  if (!f) return NULL;
  if (f->function_type)
    return ps_type_shape_matches(f->function_type, function_type)
               ? f : NULL;
  f->function_type =
      ctx_type_clone_persistent_in(context, function_type);
  return f;
}

const psx_function_symbol_t *ps_ctx_register_function_type(
    char *name, int len, const psx_type_t *function_type) {
  return ps_ctx_register_function_type_in(
      active_semantic_context, name, len, function_type);
}

int psx_ctx_track_function_type(char *name, int len,
                                const psx_type_t *function_type) {
  return ps_ctx_register_function_type(name, len, function_type) != NULL;
}

const psx_type_t *ps_ctx_get_function_type(char *name, int len) {
  return ps_function_symbol_type(
      ps_ctx_find_function_symbol(name, len));
}

int ps_ctx_format_function_signature(char *name, int len,
                                     char *out, size_t out_size) {
  const psx_type_t *type = ps_ctx_get_function_type(name, len);
  if (!type) return -1;
  return ps_type_format_canonical_signature(type, out, out_size);
}

bool psx_ctx_is_type_token(token_kind_t kind) {
  return kind == TK_INT || kind == TK_CHAR || kind == TK_VOID || kind == TK_SHORT ||
         kind == TK_LONG || kind == TK_FLOAT || kind == TK_DOUBLE ||
         kind == TK_BOOL || kind == TK_SIGNED || kind == TK_UNSIGNED ||
         kind == TK_COMPLEX || kind == TK_IMAGINARY;
}

bool psx_ctx_is_tag_keyword(token_kind_t kind) {
  return kind == TK_STRUCT || kind == TK_UNION || kind == TK_ENUM;
}

bool ps_ctx_is_tag_aggregate_kind(token_kind_t kind) {
  return kind == TK_STRUCT || kind == TK_UNION;
}

const char *ps_ctx_tag_kind_spelling(token_kind_t kind) {
  switch (kind) {
    case TK_STRUCT: return "struct";
    case TK_UNION: return "union";
    case TK_ENUM: return "enum";
    default: return "tag";
  }
}

int ps_ctx_scalar_type_size(token_kind_t kind) {
  switch (kind) {
    case TK_CHAR: return 1;
    case TK_BOOL: return 1;
    case TK_SHORT: return 2;
    case TK_INT:
    case TK_SIGNED:
    case TK_UNSIGNED:
    case TK_FLOAT:
      return 4;
    case TK_LONG:
    case TK_DOUBLE:
      return 8;
    default:
      return 8;
  }
}

void psx_ctx_get_type_info(token_kind_t kind, bool *is_type_token, int *scalar_size) {
  bool is_type = false;
  int size = 8;
  switch (kind) {
    case TK_CHAR:
    case TK_BOOL:
      is_type = true;
      size = 1;
      break;
    case TK_SHORT:
      is_type = true;
      size = 2;
      break;
    case TK_INT:
    case TK_SIGNED:
    case TK_UNSIGNED:
    case TK_FLOAT:
      is_type = true;
      size = 4;
      break;
    case TK_LONG:
    case TK_DOUBLE:
      is_type = true;
      size = 8;
      break;
    case TK_VOID:
      is_type = true;
      size = 8;
      break;
    default:
      break;
  }
  if (is_type_token) *is_type_token = is_type;
  if (scalar_size) *scalar_size = size;
}
