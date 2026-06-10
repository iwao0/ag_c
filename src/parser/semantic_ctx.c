#include "internal/semantic_ctx.h"
#include "internal/diag.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"
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

typedef struct tag_type_t tag_type_t;
struct tag_type_t {
  tag_type_t *next_hash;
  token_kind_t kind;
  char *name;
  int len;
  int member_count;
  int size;
  int scope_depth;
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
  int type_size;
  int deref_size;
  int array_len;
  token_kind_t member_tag_kind;
  char *member_tag_name;
  int member_tag_len;
  int member_is_tag_pointer;
  int bit_width;    // ビットフィールド幅（0: 非ビットフィールド）
  int bit_offset;   // ストレージユニット内ビット位置
  int bit_is_signed;
  tk_float_kind_t fp_kind;  // float/double メンバの種別 (FP store/load 用)
  int is_bool;              // 1: _Bool メンバ (代入を 0/1 に正規化する)
  int decl_order;
  int scope_depth;
};

typedef struct enum_const_t enum_const_t;
struct enum_const_t {
  enum_const_t *next_hash;
  char *name;
  int len;
  long long value;
  int scope_depth;
};
typedef struct typedef_name_t typedef_name_t;
struct typedef_name_t {
  typedef_name_t *next_hash;
  char *name;
  int len;
  token_kind_t base_kind;
  int elem_size;
  tk_float_kind_t fp_kind;
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  int is_pointer;
  int sizeof_size;
  int pointee_const_qualified;
  int pointee_volatile_qualified;
  int is_unsigned;
  // typedef した型が配列型 (例: `typedef int row_t[3]`) のときに 1。
  // 不完全配列 `typedef int A[]` でも 1（sizeof_size は 0）。
  // 仮引数 `row_t *a` を `(*a)[N]` 相当として扱うため、判別が必要。
  int is_array;
  // 配列の最も外側 `[N]` の N。多次元 `typedef int M[3][4]` で
  // 仮引数 `M *p` の mid_stride を求めるのに使う (= sizeof_size / first_dim)。
  int array_first_dim;
  // 多次元 typedef 配列の全次元数。例: `typedef int M[2][3][4]` で array_dim_count=3。
  // 0 のときは未知 (互換用; ex3 API で初めてセットされる)。
  int array_dim_count;
  // 多次元 typedef 配列の各次元のサイズ。array_dims[0] が最も外側。
  // 上限 8 次元 (実用上十分)。
  int array_dims[8];
  int scope_depth;
};
typedef struct func_name_t func_name_t;
struct func_name_t {
  func_name_t *next_hash;
  char *name;
  int len;
  int ret_struct_size; // 構造体戻り値サイズ（0: 非構造体）
  token_kind_t ret_tag_kind;
  char *ret_tag_name;
  int ret_tag_len;
  // 戻り値が float/double のときに保持する。`(int)f()` キャストで
  // codegen に ND_FP_TO_INT (fcvtzs) を挿入させるために必要。
  tk_float_kind_t ret_fp_kind;
  // variadic 関数 (`...` を持つ) かどうかと、固定引数の個数。
  // Apple ARM64 ABI に従い caller は variadic 引数を stack に積むため、
  // 呼び出し側 codegen で nargs_fixed を境に register / stack を切り替える。
  int is_variadic;
  int nargs_fixed;
  /* 1: 戻り値型が void。代入や初期化での使用を検出するのに使う (C11 6.5.16)。 */
  int is_ret_void;
  /* 戻り値型の基底情報。再宣言で型が異なる場合のエラー検出 (C11 6.7p3) に使う。
   * ret_set_once が 0 のうちは比較せず初回値として記録する。 */
  int ret_set_once;
  token_kind_t ret_token_kind;
  int ret_is_pointer;
  /* 仮引数 i の fp_kind (float/double/none) を保持。呼び出し側 IR builder が
   * `f(1)` のような int 実引数を double 仮引数に渡すケースで I2F キャスト
   * を挿入するために使う。 16 個まで track (それ以降は NONE のままで暗黙
   * 変換なし — 既存挙動)。 */
  unsigned char param_fp_kinds[16];
  int param_fp_kinds_count;
};

static goto_ref_t *goto_refs_all = NULL;
static label_def_t *label_defs_by_bucket[PCTX_HASH_BUCKETS];
static tag_type_t *tag_types_by_bucket[PCTX_HASH_BUCKETS];
static tag_member_t *tag_members_by_bucket[PCTX_HASH_BUCKETS];
static enum_const_t *enum_consts_by_bucket[PCTX_HASH_BUCKETS];
static typedef_name_t *typedefs_by_bucket[PCTX_HASH_BUCKETS];
static func_name_t *func_names_by_bucket[PCTX_HASH_BUCKETS];
static int tag_scope_depth = 0;
static int tag_member_decl_order = 0;

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
void psx_ctx_reset_function_names(void) {
  memset(func_names_by_bucket, 0, sizeof(func_names_by_bucket));
}

void psx_ctx_reset_function_scope(void) {
  goto_refs_all = NULL;
  memset(label_defs_by_bucket, 0, sizeof(label_defs_by_bucket));
  tag_scope_depth = 0;
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

void psx_ctx_enter_block_scope(void) {
  tag_scope_depth++;
}

void psx_ctx_leave_block_scope(void) {
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
  goto_ref_t *g = calloc(1, sizeof(goto_ref_t));
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
      psx_diag_duplicate_with_name(tok, diag_text_for(DIAG_TEXT_LABEL), name, len);
    }
  }
  label_def_t *d = calloc(1, sizeof(label_def_t));
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
      psx_diag_ctx(g->tok, "goto", diag_message_for(DIAG_ERR_PARSER_GOTO_LABEL_UNDEFINED),
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

bool psx_ctx_has_tag_type(token_kind_t kind, char *name, int len) {
  return find_tag_type(kind, name, len) != NULL;
}

void psx_ctx_define_tag_type(token_kind_t kind, char *name, int len) {
  psx_ctx_define_tag_type_with_layout(kind, name, len, 0, 0);
}

void psx_ctx_define_tag_type_with_members(token_kind_t kind, char *name, int len, int member_count) {
  psx_ctx_define_tag_type_with_layout(kind, name, len, member_count, member_count > 0 ? 8 : 0);
}

void psx_ctx_define_tag_type_with_layout(token_kind_t kind, char *name, int len, int member_count, int tag_size) {
  tag_type_t *existing = find_tag_type(kind, name, len);
  if (existing) {
    if (member_count > existing->member_count) existing->member_count = member_count;
    if (tag_size > existing->size) existing->size = tag_size;
    return;
  }
  unsigned bucket = psx_ctx_hash_tag(kind, name, len);
  tag_type_t *t = calloc(1, sizeof(tag_type_t));
  t->kind = kind;
  t->name = name;
  t->len = len;
  t->member_count = member_count;
  t->size = tag_size;
  t->scope_depth = tag_scope_depth;
  t->next_hash = tag_types_by_bucket[bucket];
  tag_types_by_bucket[bucket] = t;
}

int psx_ctx_get_tag_member_count(token_kind_t kind, char *name, int len) {
  tag_type_t *t = find_tag_type(kind, name, len);
  return t ? t->member_count : -1;
}

int psx_ctx_get_tag_size(token_kind_t kind, char *name, int len) {
  tag_type_t *t = find_tag_type(kind, name, len);
  return t ? t->size : -1;
}

void psx_ctx_add_tag_member_bf(token_kind_t tag_kind, char *tag_name, int tag_len,
                               char *member_name, int member_len, int offset,
                               int type_size, int deref_size, int array_len,
                               token_kind_t member_tag_kind, char *member_tag_name,
                               int member_tag_len, int member_is_tag_pointer,
                               int bit_width, int bit_offset, int bit_is_signed) {
  unsigned bucket = (psx_ctx_hash_tag(tag_kind, tag_name, tag_len) ^
                     psx_ctx_hash_name(member_name, member_len)) & (PCTX_HASH_BUCKETS - 1u);
  for (tag_member_t *m = tag_members_by_bucket[bucket]; m; m = m->next_hash) {
    if (m->tag_kind == tag_kind && m->tag_len == tag_len &&
        m->member_len == member_len &&
        strncmp(m->tag_name, tag_name, (size_t)tag_len) == 0 &&
        strncmp(m->member_name, member_name, (size_t)member_len) == 0 &&
        m->scope_depth == tag_scope_depth) {
      m->offset = offset;
      m->type_size = type_size;
      m->deref_size = deref_size;
      m->array_len = array_len;
      m->member_tag_kind = member_tag_kind;
      m->member_tag_name = member_tag_name;
      m->member_tag_len = member_tag_len;
      m->member_is_tag_pointer = member_is_tag_pointer;
      m->bit_width = bit_width;
      m->bit_offset = bit_offset;
      m->bit_is_signed = bit_is_signed;
      return;
    }
  }
  tag_member_t *m = calloc(1, sizeof(tag_member_t));
  m->tag_kind = tag_kind;
  m->tag_name = tag_name;
  m->tag_len = tag_len;
  m->member_name = member_name;
  m->member_len = member_len;
  m->offset = offset;
  m->type_size = type_size;
  m->deref_size = deref_size;
  m->array_len = array_len;
  m->member_tag_kind = member_tag_kind;
  m->member_tag_name = member_tag_name;
  m->member_tag_len = member_tag_len;
  m->member_is_tag_pointer = member_is_tag_pointer;
  m->bit_width = bit_width;
  m->bit_offset = bit_offset;
  m->bit_is_signed = bit_is_signed;
  m->decl_order = tag_member_decl_order++;
  m->scope_depth = tag_scope_depth;
  m->next_hash = tag_members_by_bucket[bucket];
  tag_members_by_bucket[bucket] = m;
}

void psx_ctx_add_tag_member(token_kind_t tag_kind, char *tag_name, int tag_len,
                            char *member_name, int member_len, int offset,
                            int type_size, int deref_size, int array_len,
                            token_kind_t member_tag_kind, char *member_tag_name,
                            int member_tag_len, int member_is_tag_pointer) {
  psx_ctx_add_tag_member_bf(tag_kind, tag_name, tag_len,
                            member_name, member_len, offset,
                            type_size, deref_size, array_len,
                            member_tag_kind, member_tag_name, member_tag_len,
                            member_is_tag_pointer, 0, 0, 0);
}

bool psx_ctx_get_tag_member_bf(token_kind_t tag_kind, char *tag_name, int tag_len,
                               char *member_name, int member_len,
                               int *out_bit_width, int *out_bit_offset, int *out_bit_is_signed) {
  unsigned bucket = (psx_ctx_hash_tag(tag_kind, tag_name, tag_len) ^
                     psx_ctx_hash_name(member_name, member_len)) & (PCTX_HASH_BUCKETS - 1u);
  for (tag_member_t *m = tag_members_by_bucket[bucket]; m; m = m->next_hash) {
    if (m->tag_kind == tag_kind && m->tag_len == tag_len &&
        m->member_len == member_len &&
        strncmp(m->tag_name, tag_name, (size_t)tag_len) == 0 &&
        strncmp(m->member_name, member_name, (size_t)member_len) == 0) {
      if (out_bit_width) *out_bit_width = m->bit_width;
      if (out_bit_offset) *out_bit_offset = m->bit_offset;
      if (out_bit_is_signed) *out_bit_is_signed = m->bit_is_signed;
      return true;
    }
  }
  return false;
}

void psx_ctx_set_tag_member_fp_kind(token_kind_t tag_kind, char *tag_name, int tag_len,
                                     char *member_name, int member_len,
                                     tk_float_kind_t fp_kind) {
  unsigned bucket = (psx_ctx_hash_tag(tag_kind, tag_name, tag_len) ^
                     psx_ctx_hash_name(member_name, member_len)) & (PCTX_HASH_BUCKETS - 1u);
  for (tag_member_t *m = tag_members_by_bucket[bucket]; m; m = m->next_hash) {
    if (m->tag_kind == tag_kind && m->tag_len == tag_len &&
        m->member_len == member_len &&
        strncmp(m->tag_name, tag_name, (size_t)tag_len) == 0 &&
        strncmp(m->member_name, member_name, (size_t)member_len) == 0) {
      m->fp_kind = fp_kind;
      return;
    }
  }
}

void psx_ctx_set_tag_member_is_bool(token_kind_t tag_kind, char *tag_name, int tag_len,
                                     char *member_name, int member_len, int is_bool) {
  unsigned bucket = (psx_ctx_hash_tag(tag_kind, tag_name, tag_len) ^
                     psx_ctx_hash_name(member_name, member_len)) & (PCTX_HASH_BUCKETS - 1u);
  for (tag_member_t *m = tag_members_by_bucket[bucket]; m; m = m->next_hash) {
    if (m->tag_kind == tag_kind && m->tag_len == tag_len &&
        m->member_len == member_len &&
        strncmp(m->tag_name, tag_name, (size_t)tag_len) == 0 &&
        strncmp(m->member_name, member_name, (size_t)member_len) == 0) {
      m->is_bool = is_bool ? 1 : 0;
      return;
    }
  }
}

int psx_ctx_get_tag_member_is_bool(token_kind_t tag_kind, char *tag_name, int tag_len,
                                    char *member_name, int member_len) {
  unsigned bucket = (psx_ctx_hash_tag(tag_kind, tag_name, tag_len) ^
                     psx_ctx_hash_name(member_name, member_len)) & (PCTX_HASH_BUCKETS - 1u);
  for (tag_member_t *m = tag_members_by_bucket[bucket]; m; m = m->next_hash) {
    if (m->tag_kind == tag_kind && m->tag_len == tag_len &&
        m->member_len == member_len &&
        strncmp(m->tag_name, tag_name, (size_t)tag_len) == 0 &&
        strncmp(m->member_name, member_name, (size_t)member_len) == 0) {
      return m->is_bool;
    }
  }
  return 0;
}

tk_float_kind_t psx_ctx_get_tag_member_fp_kind(token_kind_t tag_kind, char *tag_name, int tag_len,
                                                 char *member_name, int member_len) {
  unsigned bucket = (psx_ctx_hash_tag(tag_kind, tag_name, tag_len) ^
                     psx_ctx_hash_name(member_name, member_len)) & (PCTX_HASH_BUCKETS - 1u);
  for (tag_member_t *m = tag_members_by_bucket[bucket]; m; m = m->next_hash) {
    if (m->tag_kind == tag_kind && m->tag_len == tag_len &&
        m->member_len == member_len &&
        strncmp(m->tag_name, tag_name, (size_t)tag_len) == 0 &&
        strncmp(m->member_name, member_name, (size_t)member_len) == 0) {
      return m->fp_kind;
    }
  }
  return TK_FLOAT_KIND_NONE;
}

static int cmp_tag_member_ptr(const void *a, const void *b) {
  const tag_member_t *ma = *(const tag_member_t * const *)a;
  const tag_member_t *mb = *(const tag_member_t * const *)b;
  if (ma->offset != mb->offset) return (ma->offset < mb->offset) ? -1 : 1;
  if (ma->decl_order != mb->decl_order) return (ma->decl_order < mb->decl_order) ? -1 : 1;
  return 0;
}

bool psx_ctx_get_tag_member_at(token_kind_t tag_kind, char *tag_name, int tag_len, int index,
                               char **out_member_name, int *out_member_len,
                               int *out_offset, int *out_type_size, int *out_deref_size, int *out_array_len,
                               token_kind_t *out_member_tag_kind, char **out_member_tag_name,
                               int *out_member_tag_len, int *out_member_is_tag_pointer) {
  int cap = 8;
  int n = 0;
  tag_member_t **members = calloc((size_t)cap, sizeof(tag_member_t *));
  for (int i = 0; i < PCTX_HASH_BUCKETS; i++) {
    for (tag_member_t *m = tag_members_by_bucket[i]; m; m = m->next_hash) {
      if (m->tag_kind != tag_kind || m->tag_len != tag_len) continue;
      if (strncmp(m->tag_name, tag_name, (size_t)tag_len) != 0) continue;
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
  tag_member_t *m = members[index];
  if (out_member_name) *out_member_name = m->member_name;
  if (out_member_len) *out_member_len = m->member_len;
  if (out_offset) *out_offset = m->offset;
  if (out_type_size) *out_type_size = m->type_size;
  if (out_deref_size) *out_deref_size = m->deref_size;
  if (out_array_len) *out_array_len = m->array_len;
  if (out_member_tag_kind) *out_member_tag_kind = m->member_tag_kind;
  if (out_member_tag_name) *out_member_tag_name = m->member_tag_name;
  if (out_member_tag_len) *out_member_tag_len = m->member_tag_len;
  if (out_member_is_tag_pointer) *out_member_is_tag_pointer = m->member_is_tag_pointer;
  free(members);
  return true;
}

bool psx_ctx_find_tag_member(token_kind_t tag_kind, char *tag_name, int tag_len,
                             char *member_name, int member_len,
                             int *out_offset, int *out_type_size, int *out_deref_size, int *out_array_len,
                             token_kind_t *out_member_tag_kind, char **out_member_tag_name,
                             int *out_member_tag_len, int *out_member_is_tag_pointer) {
  unsigned bucket = (psx_ctx_hash_tag(tag_kind, tag_name, tag_len) ^
                     psx_ctx_hash_name(member_name, member_len)) & (PCTX_HASH_BUCKETS - 1u);
  for (tag_member_t *m = tag_members_by_bucket[bucket]; m; m = m->next_hash) {
    if (m->tag_kind == tag_kind && m->tag_len == tag_len &&
        m->member_len == member_len &&
        strncmp(m->tag_name, tag_name, (size_t)tag_len) == 0 &&
        strncmp(m->member_name, member_name, (size_t)member_len) == 0) {
      if (out_offset) *out_offset = m->offset;
      if (out_type_size) *out_type_size = m->type_size;
      if (out_deref_size) *out_deref_size = m->deref_size;
      if (out_array_len) *out_array_len = m->array_len;
      if (out_member_tag_kind) *out_member_tag_kind = m->member_tag_kind;
      if (out_member_tag_name) *out_member_tag_name = m->member_tag_name;
      if (out_member_tag_len) *out_member_tag_len = m->member_tag_len;
      if (out_member_is_tag_pointer) *out_member_is_tag_pointer = m->member_is_tag_pointer;
      return true;
    }
  }
  return false;
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
int psx_ctx_define_enum_const(char *name, int len, long long value) {
  enum_const_t *existing = find_enum_const_in_current_scope(name, len);
  if (existing) {
    return 0;
  }
  unsigned bucket = psx_ctx_hash_name(name, len);
  enum_const_t *e = calloc(1, sizeof(enum_const_t));
  e->name = name;
  e->len = len;
  e->value = value;
  e->scope_depth = tag_scope_depth;
  e->next_hash = enum_consts_by_bucket[bucket];
  enum_consts_by_bucket[bucket] = e;
  return 1;
}

bool psx_ctx_find_enum_const(char *name, int len, long long *out_value) {
  enum_const_t *e = find_enum_const(name, len);
  if (!e) return false;
  if (out_value) *out_value = e->value;
  return true;
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

static void assign_typedef_fields(typedef_name_t *t, token_kind_t base_kind, int elem_size,
                                  tk_float_kind_t fp_kind, token_kind_t tag_kind,
                                  char *tag_name, int tag_len, int is_pointer, int sizeof_size,
                                  int pointee_const_qualified, int pointee_volatile_qualified,
                                  int is_unsigned, int is_array, int array_first_dim) {
  t->base_kind = base_kind;
  t->elem_size = elem_size;
  t->fp_kind = fp_kind;
  t->tag_kind = tag_kind;
  t->tag_name = tag_name;
  t->tag_len = tag_len;
  t->is_pointer = is_pointer;
  t->sizeof_size = sizeof_size;
  t->pointee_const_qualified = pointee_const_qualified;
  t->pointee_volatile_qualified = pointee_volatile_qualified;
  t->is_unsigned = is_unsigned;
  t->is_array = is_array;
  t->array_first_dim = array_first_dim;
}

int psx_ctx_define_typedef_name(char *name, int len, token_kind_t base_kind, int elem_size,
                                 tk_float_kind_t fp_kind, token_kind_t tag_kind,
                                 char *tag_name, int tag_len, int is_pointer, int sizeof_size,
                                 int pointee_const_qualified, int pointee_volatile_qualified,
                                 int is_unsigned) {
  return psx_ctx_define_typedef_name_ex(name, len, base_kind, elem_size, fp_kind, tag_kind,
                                 tag_name, tag_len, is_pointer, sizeof_size,
                                 pointee_const_qualified, pointee_volatile_qualified,
                                 is_unsigned, 0);
}

int psx_ctx_define_typedef_name_ex(char *name, int len, token_kind_t base_kind, int elem_size,
                                    tk_float_kind_t fp_kind, token_kind_t tag_kind,
                                    char *tag_name, int tag_len, int is_pointer, int sizeof_size,
                                    int pointee_const_qualified, int pointee_volatile_qualified,
                                    int is_unsigned, int is_array) {
  return psx_ctx_define_typedef_name_ex2(name, len, base_kind, elem_size, fp_kind, tag_kind,
                                  tag_name, tag_len, is_pointer, sizeof_size,
                                  pointee_const_qualified, pointee_volatile_qualified,
                                  is_unsigned, is_array, 0);
}

int psx_ctx_define_typedef_name_ex2(char *name, int len, token_kind_t base_kind, int elem_size,
                                     tk_float_kind_t fp_kind, token_kind_t tag_kind,
                                     char *tag_name, int tag_len, int is_pointer, int sizeof_size,
                                     int pointee_const_qualified, int pointee_volatile_qualified,
                                     int is_unsigned, int is_array, int array_first_dim) {
  return psx_ctx_define_typedef_name_ex3(name, len, base_kind, elem_size, fp_kind, tag_kind,
                                  tag_name, tag_len, is_pointer, sizeof_size,
                                  pointee_const_qualified, pointee_volatile_qualified,
                                  is_unsigned, is_array, array_first_dim, NULL, 0);
}

int psx_ctx_define_typedef_name_ex3(char *name, int len, token_kind_t base_kind, int elem_size,
                                     tk_float_kind_t fp_kind, token_kind_t tag_kind,
                                     char *tag_name, int tag_len, int is_pointer, int sizeof_size,
                                     int pointee_const_qualified, int pointee_volatile_qualified,
                                     int is_unsigned, int is_array, int array_first_dim,
                                     const int *array_dims, int array_dim_count) {
  typedef_name_t *existing = find_typedef_in_current_scope(name, len);
  /* C11 6.7p3: typedef は同じ型なら再宣言可。違う型なら error。
   * 比較するフィールドは「型の identity」を成すもの。tag_name は同じ ptr で
   * あるはずなので ptr 比較で十分 (parser が tag を共有させている)。 */
  if (existing) {
    int n_new = (array_dim_count < 0) ? 0 : array_dim_count;
    if (n_new > 8) n_new = 8;
    int same = (existing->base_kind == base_kind &&
                existing->elem_size == elem_size &&
                existing->fp_kind == fp_kind &&
                existing->tag_kind == tag_kind &&
                existing->tag_name == tag_name &&
                existing->tag_len == tag_len &&
                existing->is_pointer == is_pointer &&
                existing->sizeof_size == sizeof_size &&
                existing->pointee_const_qualified == pointee_const_qualified &&
                existing->pointee_volatile_qualified == pointee_volatile_qualified &&
                existing->is_unsigned == is_unsigned &&
                existing->is_array == is_array &&
                existing->array_first_dim == array_first_dim &&
                existing->array_dim_count == n_new);
    if (same && array_dims) {
      for (int i = 0; i < n_new; i++) {
        if (existing->array_dims[i] != array_dims[i]) { same = 0; break; }
      }
    }
    if (!same) return 0;
    return 1;  /* 同じ型なら登録済みのままで OK */
  }
  unsigned bucket = psx_ctx_hash_name(name, len);
  typedef_name_t *t = calloc(1, sizeof(typedef_name_t));
  t->name = name;
  t->len = len;
  t->scope_depth = tag_scope_depth;
  t->next_hash = typedefs_by_bucket[bucket];
  typedefs_by_bucket[bucket] = t;
  assign_typedef_fields(t, base_kind, elem_size, fp_kind, tag_kind,
                        tag_name, tag_len, is_pointer, sizeof_size,
                        pointee_const_qualified, pointee_volatile_qualified, is_unsigned,
                        is_array, array_first_dim);
  int n = (array_dim_count < 0) ? 0 : array_dim_count;
  if (n > 8) n = 8;
  t->array_dim_count = n;
  for (int i = 0; i < n; i++) t->array_dims[i] = array_dims ? array_dims[i] : 0;
  for (int i = n; i < 8; i++) t->array_dims[i] = 0;
  return 1;
}

bool psx_ctx_find_typedef_sizeof(char *name, int len, int *out_sizeof_size) {
  typedef_name_t *t = find_typedef(name, len);
  if (!t) return false;
  if (out_sizeof_size) *out_sizeof_size = t->sizeof_size;
  return true;
}

bool psx_ctx_find_typedef_name(char *name, int len, token_kind_t *out_base_kind,
                               int *out_elem_size, tk_float_kind_t *out_fp_kind,
                               token_kind_t *out_tag_kind, char **out_tag_name,
                               int *out_tag_len, int *out_is_pointer,
                               int *out_pointee_const_qualified, int *out_pointee_volatile_qualified,
                               int *out_is_unsigned) {
  return psx_ctx_find_typedef_name_ex(name, len, out_base_kind, out_elem_size, out_fp_kind,
                                      out_tag_kind, out_tag_name, out_tag_len, out_is_pointer,
                                      out_pointee_const_qualified, out_pointee_volatile_qualified,
                                      out_is_unsigned, NULL, NULL);
}

bool psx_ctx_find_typedef_name_ex(char *name, int len, token_kind_t *out_base_kind,
                                  int *out_elem_size, tk_float_kind_t *out_fp_kind,
                                  token_kind_t *out_tag_kind, char **out_tag_name,
                                  int *out_tag_len, int *out_is_pointer,
                                  int *out_pointee_const_qualified,
                                  int *out_pointee_volatile_qualified, int *out_is_unsigned,
                                  int *out_is_array, int *out_sizeof_size) {
  return psx_ctx_find_typedef_name_ex2(name, len, out_base_kind, out_elem_size, out_fp_kind,
                                       out_tag_kind, out_tag_name, out_tag_len, out_is_pointer,
                                       out_pointee_const_qualified,
                                       out_pointee_volatile_qualified, out_is_unsigned,
                                       out_is_array, out_sizeof_size, NULL);
}

bool psx_ctx_find_typedef_name_ex2(char *name, int len, token_kind_t *out_base_kind,
                                   int *out_elem_size, tk_float_kind_t *out_fp_kind,
                                   token_kind_t *out_tag_kind, char **out_tag_name,
                                   int *out_tag_len, int *out_is_pointer,
                                   int *out_pointee_const_qualified,
                                   int *out_pointee_volatile_qualified, int *out_is_unsigned,
                                   int *out_is_array, int *out_sizeof_size,
                                   int *out_array_first_dim) {
  return psx_ctx_find_typedef_name_ex3(name, len, out_base_kind, out_elem_size, out_fp_kind,
                                       out_tag_kind, out_tag_name, out_tag_len, out_is_pointer,
                                       out_pointee_const_qualified,
                                       out_pointee_volatile_qualified, out_is_unsigned,
                                       out_is_array, out_sizeof_size, out_array_first_dim,
                                       NULL, NULL, 0);
}

bool psx_ctx_find_typedef_name_ex3(char *name, int len, token_kind_t *out_base_kind,
                                   int *out_elem_size, tk_float_kind_t *out_fp_kind,
                                   token_kind_t *out_tag_kind, char **out_tag_name,
                                   int *out_tag_len, int *out_is_pointer,
                                   int *out_pointee_const_qualified,
                                   int *out_pointee_volatile_qualified, int *out_is_unsigned,
                                   int *out_is_array, int *out_sizeof_size,
                                   int *out_array_first_dim,
                                   int *out_array_dims, int *out_array_dim_count,
                                   int max_dims) {
  typedef_name_t *t = find_typedef(name, len);
  if (!t) return false;
  if (out_base_kind) *out_base_kind = t->base_kind;
  if (out_elem_size) *out_elem_size = t->elem_size;
  if (out_fp_kind) *out_fp_kind = t->fp_kind;
  if (out_tag_kind) *out_tag_kind = t->tag_kind;
  if (out_tag_name) *out_tag_name = t->tag_name;
  if (out_tag_len) *out_tag_len = t->tag_len;
  if (out_is_pointer) *out_is_pointer = t->is_pointer;
  if (out_pointee_const_qualified) *out_pointee_const_qualified = t->pointee_const_qualified;
  if (out_pointee_volatile_qualified) *out_pointee_volatile_qualified = t->pointee_volatile_qualified;
  if (out_is_unsigned) *out_is_unsigned = t->is_unsigned;
  if (out_is_array) *out_is_array = t->is_array;
  if (out_sizeof_size) *out_sizeof_size = t->sizeof_size;
  if (out_array_first_dim) *out_array_first_dim = t->array_first_dim;
  if (out_array_dim_count) {
    int n = t->array_dim_count;
    if (max_dims > 0 && n > max_dims) n = max_dims;
    *out_array_dim_count = n;
    if (out_array_dims) {
      for (int i = 0; i < n; i++) out_array_dims[i] = t->array_dims[i];
    }
  }
  return true;
}

bool psx_ctx_is_typedef_name_token(token_t *tok) {
  if (!tok || tok->kind != TK_IDENT) return false;
  token_ident_t *id = (token_ident_t *)tok;
  return psx_ctx_find_typedef_name(id->str, id->len, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
}

void psx_ctx_define_function_name(char *name, int len) {
  psx_ctx_define_function_name_with_ret(name, len, 0);
}

// 任意のスコープから名前一致の関数名エントリを返す。なければ NULL。
static func_name_t *find_function_name(char *name, int len) {
  unsigned bucket = psx_ctx_hash_name(name, len);
  for (func_name_t *f = func_names_by_bucket[bucket]; f; f = f->next_hash) {
    if (f->len == len && strncmp(f->name, name, (size_t)len) == 0) {
      return f;
    }
  }
  return NULL;
}

void psx_ctx_define_function_name_with_ret(char *name, int len, int ret_struct_size) {
  func_name_t *existing = find_function_name(name, len);
  if (existing) {
    existing->ret_struct_size = ret_struct_size; // 更新
    return;
  }
  unsigned bucket = psx_ctx_hash_name(name, len);
  func_name_t *f = calloc(1, sizeof(func_name_t));
  f->name = name;
  f->len = len;
  f->ret_struct_size = ret_struct_size;
  f->ret_tag_kind = TK_EOF;
  f->ret_tag_name = NULL;
  f->ret_tag_len = 0;
  f->next_hash = func_names_by_bucket[bucket];
  func_names_by_bucket[bucket] = f;
}

void psx_ctx_set_function_ret_tag(char *name, int len, token_kind_t tag_kind, char *tag_name, int tag_len) {
  func_name_t *f = find_function_name(name, len);
  if (!f) return;
  f->ret_tag_kind = tag_kind;
  f->ret_tag_name = tag_name;
  f->ret_tag_len = tag_len;
}

bool psx_ctx_has_function_name(char *name, int len) {
  return find_function_name(name, len) != NULL;
}

int psx_ctx_get_function_ret_struct_size(char *name, int len) {
  func_name_t *f = find_function_name(name, len);
  return f ? f->ret_struct_size : 0;
}

void psx_ctx_set_function_ret_fp_kind(char *name, int len, tk_float_kind_t fp_kind) {
  func_name_t *f = find_function_name(name, len);
  if (!f) return;
  f->ret_fp_kind = fp_kind;
}

tk_float_kind_t psx_ctx_get_function_ret_fp_kind(char *name, int len) {
  func_name_t *f = find_function_name(name, len);
  return f ? f->ret_fp_kind : TK_FLOAT_KIND_NONE;
}

void psx_ctx_set_function_param_fp_kind(char *name, int len, int param_idx,
                                         tk_float_kind_t fp_kind) {
  func_name_t *f = find_function_name(name, len);
  if (!f) return;
  if (param_idx < 0 || param_idx >= 16) return;
  f->param_fp_kinds[param_idx] = (unsigned char)fp_kind;
  if (param_idx + 1 > f->param_fp_kinds_count) {
    f->param_fp_kinds_count = param_idx + 1;
  }
}

tk_float_kind_t psx_ctx_get_function_param_fp_kind(char *name, int len, int param_idx) {
  func_name_t *f = find_function_name(name, len);
  if (!f) return TK_FLOAT_KIND_NONE;
  if (param_idx < 0 || param_idx >= f->param_fp_kinds_count) return TK_FLOAT_KIND_NONE;
  return (tk_float_kind_t)f->param_fp_kinds[param_idx];
}

void psx_ctx_set_function_variadic(char *name, int len, int is_variadic, int nargs_fixed) {
  func_name_t *f = find_function_name(name, len);
  if (!f) return;
  f->is_variadic = is_variadic;
  f->nargs_fixed = nargs_fixed;
}

void psx_ctx_set_function_ret_void(char *name, int len, int is_void) {
  func_name_t *f = find_function_name(name, len);
  if (!f) return;
  f->is_ret_void = is_void ? 1 : 0;
}

bool psx_ctx_is_function_ret_void(char *name, int len) {
  func_name_t *f = find_function_name(name, len);
  return f && f->is_ret_void != 0;
}

/* 関数の戻り値型 (基底 token_kind と pointer フラグ) を登録/比較する。
 * 既に同名で登録があれば、新しい値と異なるか確認する。
 * 戻り値: 1 = OK (新規 or 互換)、0 = 衝突 (呼び出し元で診断発行)。 */
int psx_ctx_get_function_ret_is_pointer(char *name, int len) {
  func_name_t *f = find_function_name(name, len);
  return (f && f->ret_set_once) ? f->ret_is_pointer : 0;
}

int psx_ctx_track_function_ret_type(char *name, int len,
                                     token_kind_t ret_token_kind, int ret_is_pointer) {
  func_name_t *f = find_function_name(name, len);
  if (!f) return 1;
  if (!f->ret_set_once) {
    f->ret_set_once = 1;
    f->ret_token_kind = ret_token_kind;
    f->ret_is_pointer = ret_is_pointer ? 1 : 0;
    return 1;
  }
  if (f->ret_token_kind == ret_token_kind &&
      f->ret_is_pointer == (ret_is_pointer ? 1 : 0)) {
    return 1;
  }
  return 0;
}

bool psx_ctx_get_function_is_variadic(char *name, int len, int *out_nargs_fixed) {
  func_name_t *f = find_function_name(name, len);
  if (!f) {
    if (out_nargs_fixed) *out_nargs_fixed = 0;
    return false;
  }
  if (out_nargs_fixed) *out_nargs_fixed = f->nargs_fixed;
  return f->is_variadic != 0;
}

void psx_ctx_get_function_ret_tag(char *name, int len, token_kind_t *out_tag_kind,
                                  char **out_tag_name, int *out_tag_len) {
  if (out_tag_kind) *out_tag_kind = TK_EOF;
  if (out_tag_name) *out_tag_name = NULL;
  if (out_tag_len) *out_tag_len = 0;
  func_name_t *f = find_function_name(name, len);
  if (!f) return;
  if (out_tag_kind) *out_tag_kind = f->ret_tag_kind;
  if (out_tag_name) *out_tag_name = f->ret_tag_name;
  if (out_tag_len) *out_tag_len = f->ret_tag_len;
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

int psx_ctx_scalar_type_size(token_kind_t kind) {
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
