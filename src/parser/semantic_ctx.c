#include "semantic_ctx.h"
#include "../semantic/resolution_store.h"
#include "type_owned_internal.h"
#include "diag.h"
#include "type.h"
#include "type_builder.h"
#include "../diag/diag.h"
#include "../semantic/type_identity.h"
#include "../semantic/record_layout.h"
#include "../tokenizer/tokenizer.h"
#include "../target_info.h"
#include "../type_layout.h"
#include <stdlib.h>
#include <string.h>

typedef struct deferred_parser_diagnostic_t deferred_parser_diagnostic_t;
struct deferred_parser_diagnostic_t {
  deferred_parser_diagnostic_t *next_all;
  const token_t *tok;
  const char *name;
};

typedef struct tag_type_t tag_type_t;
struct tag_type_t {
  token_kind_t kind;
  char *name;
  int len;
  unsigned char enum_is_complete;
  psx_scope_id_t member_scope_id;
  psx_record_decl_t *record_decl;
  psx_record_member_decl_t *record_decl_members;
};

static int tag_type_is_complete(const tag_type_t *tag) {
  if (!tag) return 0;
  if (tag->kind == TK_ENUM) return tag->enum_is_complete ? 1 : 0;
  return tag->record_decl && tag->record_decl->is_complete;
}
typedef struct tag_member_t tag_member_t;
typedef struct tag_member_decl_t {
  char *name;
  int len;
  int bit_width;
  int bit_is_signed;
  const psx_semantic_type_table_t *type_table;
  psx_qual_type_t qual_type;
} tag_member_decl_t;

struct tag_member_t {
  tag_member_decl_t declaration;
};

typedef struct tag_member_layout_draft_t tag_member_layout_draft_t;
/* Definition-time placement is target-specific. Completed layouts are copied
 * into RecordLayoutTable and never become part of the member declaration. */
struct tag_member_layout_draft_t {
  tag_member_layout_draft_t *next;
  const tag_member_t *member;
  ag_data_layout_t data_layout;
  psx_record_member_layout_t placement;
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

static bool get_tag_member_impl_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len, int scope_depth,
    int index, psx_record_member_decl_t *out_declaration,
    psx_record_member_layout_t *out_layout);
static int collect_tag_member_declarations_in(
    psx_semantic_context_t *context, const tag_type_t *tag,
    tag_member_t ***out_members);

static void refresh_cached_record_decl(
    psx_semantic_context_t *context, tag_type_t *tag) {
  if (!context || !tag || !tag->record_decl) return;
  psx_record_decl_t *record_decl = tag->record_decl;
  tag_member_t **source_members = NULL;
  int member_count = collect_tag_member_declarations_in(
      context, tag, &source_members);
  if (member_count < 0) return;
  psx_record_member_decl_t *members = NULL;
  if (member_count > 0) {
    members = ctx_calloc_in(
        context, (size_t)member_count, sizeof(*members));
    if (!members) {
      free(source_members);
      return;
    }
    for (int i = 0; i < member_count; i++) {
      tag_member_t *member = source_members[i];
      members[i] = (psx_record_member_decl_t){
          .name = member->declaration.name,
          .len = member->declaration.len,
          .bit_width = member->declaration.bit_width,
          .bit_is_signed = member->declaration.bit_is_signed,
          .decl_type_table = member->declaration.type_table,
          .decl_qual_type = member->declaration.qual_type,
      };
    }
  }
  free(source_members);

  ctx_release_in(context, tag->record_decl_members);
  tag->record_decl_members = members;
  record_decl->record_kind = ps_type_kind_from_tag_kind(tag->kind);
  record_decl->tag_name = tag->name;
  record_decl->tag_len = tag->len;
  record_decl->member_count = member_count;
  record_decl->members = tag->record_decl_members;
}

typedef struct enum_const_t enum_const_t;
struct enum_const_t {
  long long value;
};
typedef struct typedef_name_t typedef_name_t;
struct typedef_name_t {
  psx_qual_type_t decl_qual_type;
  psx_runtime_declarator_application_t *runtime_application;
};

static psx_qual_type_t typedef_record_decl_qual_type(
    const typedef_name_t *t) {
  return t ? t->decl_qual_type
           : (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                               PSX_TYPE_QUALIFIER_NONE};
}

static const psx_type_t *typedef_record_decl_type(
    const psx_semantic_context_t *context, const typedef_name_t *t) {
  return psx_semantic_type_table_lookup_qual_type(
      ps_ctx_semantic_type_table_in(context),
      typedef_record_decl_qual_type(t));
}

static const psx_runtime_declarator_application_t *
typedef_record_runtime_application(const typedef_name_t *t) {
  return t ? t->runtime_application : NULL;
}

struct psx_function_symbol_t {
  psx_qual_type_t function_qual_type;
  /* 1: この関数名はすでに本体定義済み。2 度目の定義を E3064 で弾くために使う
   * (C11 6.9p3、`int f(){...} int f(){...}` 等)。プロトタイプ宣言 `int f(int);`
   * のみではこのフラグは立たない。 */
  int is_defined;
};

struct psx_semantic_context_t {
  arena_context_t *arena_context;
  ag_diagnostic_context_t *diagnostic_context;
  psx_resolution_store_t *resolution_store;
  psx_scope_graph_t *scope_graph;
  const ag_target_info_t *target;
  psx_record_id_t next_record_id;
  psx_semantic_expression_table_t *semantic_expressions;
  psx_semantic_type_table_t *semantic_types;
  psx_record_decl_table_t *record_decls;
  psx_record_layout_table_t *record_layouts;
  psx_ctx_allocation_t *allocations;
  deferred_parser_diagnostic_t *pending_diagnostics_all;
  tag_member_layout_draft_t *aggregate_member_layout_drafts;
};

static psx_function_symbol_t *function_symbol_from_declaration(
    const psx_scope_declaration_t *declaration) {
  return declaration &&
                 declaration->name_space == PSX_NAMESPACE_ORDINARY &&
                 declaration->kind == PSX_DECL_FUNCTION
             ? declaration->payload
             : NULL;
}

static const psx_scope_declaration_t *tag_declaration_for_payload_in(
    const psx_semantic_context_t *context, const tag_type_t *tag) {
  if (!context || !context->scope_graph || !tag) return NULL;
  size_t declaration_count = psx_scope_graph_declaration_count(
      context->scope_graph);
  for (size_t index = declaration_count; index > 0; index--) {
    const psx_scope_declaration_t *declaration =
        psx_scope_graph_declaration_at(context->scope_graph, index - 1);
    if (declaration && declaration->name_space == PSX_NAMESPACE_TAG &&
        declaration->kind == PSX_DECL_TAG &&
        declaration->payload == tag)
      return declaration;
  }
  return NULL;
}

static int tag_scope_depth_in(
    const psx_semantic_context_t *context, const tag_type_t *tag) {
  const psx_scope_declaration_t *declaration =
      tag_declaration_for_payload_in(context, tag);
  return declaration
             ? psx_scope_graph_scope_depth(
                   context->scope_graph, declaration->scope_id)
             : -1;
}

static int collect_tag_member_declarations_in(
    psx_semantic_context_t *context, const tag_type_t *tag,
    tag_member_t ***out_members) {
  if (out_members) *out_members = NULL;
  if (!context || !context->scope_graph || !tag || !out_members ||
      tag->member_scope_id == PSX_SCOPE_ID_INVALID)
    return -1;

  int capacity = 8;
  int count = 0;
  tag_member_t **members = malloc(
      (size_t)capacity * sizeof(*members));
  if (!members) return -1;
  size_t declaration_count = psx_scope_graph_declaration_count(
      context->scope_graph);
  for (size_t index = 0; index < declaration_count; index++) {
    const psx_scope_declaration_t *declaration =
        psx_scope_graph_declaration_at(context->scope_graph, index);
    if (!declaration || declaration->scope_id != tag->member_scope_id ||
        declaration->name_space != PSX_NAMESPACE_MEMBER ||
        declaration->kind != PSX_DECL_MEMBER || !declaration->payload)
      continue;
    if (count == capacity) {
      capacity *= 2;
      tag_member_t **grown = realloc(
          members, (size_t)capacity * sizeof(*members));
      if (!grown) {
        free(members);
        return -1;
      }
      members = grown;
    }
    members[count++] = declaration->payload;
  }
  *out_members = members;
  return count;
}

static psx_record_id_t allocate_record_id(
    psx_semantic_context_t *context) {
  if (!context) return PSX_RECORD_ID_INVALID;
  context->next_record_id++;
  if (context->next_record_id == PSX_RECORD_ID_INVALID)
    context->next_record_id++;
  return context->next_record_id;
}

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

static void ctx_release_all(psx_semantic_context_t *context) {
  if (!context) return;
  while (context->allocations) {
    psx_ctx_allocation_t *allocation = context->allocations;
    context->allocations = allocation->next;
    free(allocation->pointer);
    free(allocation);
  }
}

psx_semantic_expr_id_t ps_ctx_register_semantic_expression_in(
    psx_semantic_context_t *context,
    const psx_typed_hir_tree_t *expression) {
  return context
             ? psx_semantic_expression_table_register(
                   context->semantic_expressions, expression)
             : PSX_SEMANTIC_EXPR_ID_INVALID;
}

const psx_typed_hir_tree_t *ps_ctx_semantic_expression_in(
    const psx_semantic_context_t *context,
    psx_semantic_expr_id_t expression_id) {
  return context
             ? psx_semantic_expression_table_lookup(
                   context->semantic_expressions, expression_id)
             : NULL;
}

psx_qual_type_t ps_ctx_intern_qual_type_in(
    psx_semantic_context_t *context, const psx_type_t *type) {
  if (!context) {
    return (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                             PSX_TYPE_QUALIFIER_NONE};
  }
  return psx_semantic_type_table_intern(context->semantic_types, type);
}

psx_qual_type_t ps_ctx_intern_integer_qual_type_in(
    psx_semantic_context_t *context,
    psx_integer_kind_t integer_kind, int is_unsigned,
    int is_plain_char) {
  if (!context) {
    return (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                             PSX_TYPE_QUALIFIER_NONE};
  }
  const psx_type_t *type = ps_type_new_integer_kind_in(
      context->arena_context, integer_kind, is_unsigned,
      is_plain_char);
  return ps_ctx_intern_qual_type_in(context, type);
}

psx_qual_type_t ps_ctx_intern_void_qual_type_in(
    psx_semantic_context_t *context) {
  if (!context) {
    return (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                             PSX_TYPE_QUALIFIER_NONE};
  }
  return ps_ctx_intern_qual_type_in(
      context,
      ps_type_new_in(context->arena_context, PSX_TYPE_VOID));
}

psx_qual_type_t ps_ctx_intern_pointer_to_qual_type_in(
    psx_semantic_context_t *context, psx_qual_type_t pointee) {
  return context
             ? psx_semantic_type_table_intern_pointer_to(
                   context->semantic_types, pointee)
             : (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                                 PSX_TYPE_QUALIFIER_NONE};
}

psx_qual_type_t ps_ctx_intern_implicit_function_qual_type_in(
    psx_semantic_context_t *context) {
  if (!context) {
    return (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                             PSX_TYPE_QUALIFIER_NONE};
  }
  psx_qual_type_t result = ps_ctx_intern_integer_qual_type_in(
      context, PSX_INTEGER_KIND_INT, 0, 0);
  const psx_type_t *result_type = ps_ctx_type_by_id_in(
      context, result.type_id);
  return ps_ctx_intern_qual_type_in(
      context,
      ps_type_new_function_in(context->arena_context, result_type));
}

psx_qual_type_t ps_ctx_find_interned_qual_type_in(
    const psx_semantic_context_t *context, const psx_type_t *type) {
  if (!context) {
    return (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                             PSX_TYPE_QUALIFIER_NONE};
  }
  return psx_semantic_type_table_find(context->semantic_types, type);
}

const psx_type_t *ps_ctx_type_by_id_in(
    const psx_semantic_context_t *context, psx_type_id_t type_id) {
  return context
             ? psx_semantic_type_table_lookup(context->semantic_types, type_id)
             : NULL;
}

const psx_semantic_type_table_t *ps_ctx_semantic_type_table_in(
    const psx_semantic_context_t *context) {
  return context ? context->semantic_types : NULL;
}

const psx_record_decl_table_t *ps_ctx_record_decl_table_in(
    const psx_semantic_context_t *context) {
  return context ? context->record_decls : NULL;
}

const psx_record_layout_table_t *ps_ctx_record_layout_table_in(
    const psx_semantic_context_t *context) {
  return context ? context->record_layouts : NULL;
}

psx_semantic_context_t *ps_ctx_create(
    arena_context_t *arena_context,
    ag_diagnostic_context_t *diagnostic_context,
    psx_resolution_store_t *resolution_store,
    psx_scope_graph_t *scope_graph,
    const ag_target_info_t *target) {
  if (!arena_context || !diagnostic_context || !resolution_store ||
      !scope_graph || !ag_target_info_is_valid(target))
    return NULL;
  psx_semantic_context_t *context = calloc(1, sizeof(*context));
  if (context) {
    context->semantic_expressions =
        psx_semantic_expression_table_create();
    context->semantic_types = psx_semantic_type_table_create();
    context->record_decls = psx_record_decl_table_create();
    context->record_layouts = psx_record_layout_table_create();
    if (!context->semantic_expressions || !context->semantic_types ||
        !context->record_decls || !context->record_layouts) {
      psx_semantic_expression_table_destroy(context->semantic_expressions);
      psx_semantic_type_table_destroy(context->semantic_types);
      psx_record_decl_table_destroy(context->record_decls);
      psx_record_layout_table_destroy(context->record_layouts);
      free(context);
      return NULL;
    }
    psx_semantic_type_table_bind_record_decls(
        context->semantic_types, context->record_decls);
    psx_resolution_store_bind_semantic_types(
        resolution_store, context->semantic_types);
    context->arena_context = arena_context;
    context->diagnostic_context = diagnostic_context;
    context->resolution_store = resolution_store;
    context->scope_graph = scope_graph;
    context->target = target;
  }
  return context;
}

void ps_ctx_destroy(psx_semantic_context_t *context) {
  if (!context) return;
  ctx_release_all(context);
  psx_semantic_expression_table_destroy(context->semantic_expressions);
  if (psx_resolution_store_semantic_types(context->resolution_store) ==
      context->semantic_types)
    psx_resolution_store_bind_semantic_types(
        context->resolution_store, NULL);
  psx_semantic_type_table_destroy(context->semantic_types);
  psx_record_decl_table_destroy(context->record_decls);
  psx_record_layout_table_destroy(context->record_layouts);
  free(context);
}

arena_context_t *ps_ctx_arena(
    const psx_semantic_context_t *context) {
  return context ? context->arena_context : NULL;
}

psx_resolution_store_t *ps_ctx_resolution_store(
    const psx_semantic_context_t *context) {
  return context ? context->resolution_store : NULL;
}

psx_scope_graph_t *ps_ctx_scope_graph(
    const psx_semantic_context_t *context) {
  return context ? context->scope_graph : NULL;
}

ag_diagnostic_context_t *ps_ctx_diagnostics(
    const psx_semantic_context_t *context) {
  return context ? context->diagnostic_context : NULL;
}

const ag_target_info_t *ps_ctx_target_info(
    const psx_semantic_context_t *context) {
  return context ? context->target : NULL;
}

const ag_data_layout_t *
ps_ctx_data_layout(const psx_semantic_context_t *context) {
  return ag_target_info_data_layout(ps_ctx_target_info(context));
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

static const psx_record_member_layout_t *find_tag_member_layout_draft(
    const psx_semantic_context_t *context, const tag_member_t *member) {
  if (!context || !member) return NULL;
  for (const tag_member_layout_draft_t *draft =
           context->aggregate_member_layout_drafts;
       draft; draft = draft->next) {
    if (draft->member == member &&
        ag_data_layout_equal(&draft->data_layout, ps_ctx_data_layout(context)))
      return &draft->placement;
  }
  return NULL;
}

static int initialize_tag_member_record(
    psx_semantic_context_t *context,
    tag_member_t *m,
    const psx_record_member_decl_t *declaration,
    const psx_record_member_layout_t *layout) {
  if (!context || !m || !declaration || !layout ||
      m->declaration.qual_type.type_id != PSX_TYPE_ID_INVALID)
    return 0;
  m->declaration.bit_width = declaration->bit_width;
  m->declaration.bit_is_signed = declaration->bit_is_signed;
  const psx_type_t *desc_type = psx_record_member_decl_type(declaration);
  psx_type_t *resolved_type = ctx_type_clone_persistent_in(
      context, desc_type);
  if (!resolved_type) return 0;
  ps_ctx_bind_record_ids_in(context, resolved_type);
  psx_qual_type_t identity = ps_ctx_intern_qual_type_in(
      context, resolved_type);
  if (identity.type_id == PSX_TYPE_ID_INVALID) return 0;
  if (!psx_semantic_type_table_lookup_qual_type(
          context->semantic_types, identity))
    return 0;
  m->declaration.type_table = context->semantic_types;
  m->declaration.qual_type = identity;
  tag_member_layout_draft_t *draft = ctx_calloc_in(
      context, 1, sizeof(*draft));
  if (!draft) return 0;
  draft->member = m;
  draft->data_layout = *ps_ctx_data_layout(context);
  draft->placement = *layout;
  draft->next = context->aggregate_member_layout_drafts;
  context->aggregate_member_layout_drafts = draft;
  return 1;
}

/* 翻訳単位 (program) の境界で関数名テーブルを初期化する。
 * テストでは fork() 経由で複数のプログラムを 1 プロセス内で解析するため、
 * 関数戻り値型チェック等が前テストの登録に引きずられないようにする。 */
void ps_ctx_reset_function_names_in(psx_semantic_context_t *context) {
  if (!context || !context->scope_graph) return;
  size_t declaration_count = psx_scope_graph_declaration_count(
      context->scope_graph);
  for (size_t index = 0; index < declaration_count; index++) {
    const psx_scope_declaration_t *declaration =
        psx_scope_graph_declaration_at(context->scope_graph, index);
    psx_function_symbol_t *function =
        function_symbol_from_declaration(declaration);
    if (!function) continue;
    psx_decl_id_t declaration_id = declaration->id;
    psx_scope_graph_forget_declaration(
        context->scope_graph, declaration_id);
    ctx_release_in(context, function);
  }
}

void ps_ctx_reset_translation_unit_scope_in(
    psx_semantic_context_t *context) {
  if (!context) return;
  arena_context_t *arena_context = context->arena_context;
  ag_diagnostic_context_t *diagnostic_context = context->diagnostic_context;
  psx_resolution_store_t *resolution_store = context->resolution_store;
  psx_scope_graph_t *scope_graph = context->scope_graph;
  const ag_target_info_t *target = context->target;
  psx_semantic_expression_table_t *semantic_expressions =
      context->semantic_expressions;
  psx_semantic_type_table_t *semantic_types = context->semantic_types;
  psx_record_decl_table_t *record_decls = context->record_decls;
  psx_record_layout_table_t *record_layouts = context->record_layouts;
  ctx_release_all(context);
  memset(context, 0, sizeof(*context));
  context->arena_context = arena_context;
  context->diagnostic_context = diagnostic_context;
  context->resolution_store = resolution_store;
  context->scope_graph = scope_graph;
  context->target = target;
  context->semantic_expressions = semantic_expressions;
  context->semantic_types = semantic_types;
  context->record_decls = record_decls;
  context->record_layouts = record_layouts;
  psx_semantic_expression_table_reset(semantic_expressions);
  psx_semantic_type_table_reset(semantic_types);
  psx_record_decl_table_reset(record_decls);
  psx_record_layout_table_reset(record_layouts);
}

void ps_ctx_record_unsupported_gnu_extension_in(
    psx_semantic_context_t *context,
    const token_t *tok, const char *name) {
  if (!context) return;
  deferred_parser_diagnostic_t *diagnostic = ctx_calloc_in(
      context, 1, sizeof(deferred_parser_diagnostic_t));
  if (!diagnostic) {
    diag_emit_internalf_in(
        context->diagnostic_context, DIAG_ERR_INTERNAL_OOM, "%s",
        diag_message_for_in(context->diagnostic_context,
                            DIAG_ERR_INTERNAL_OOM));
    return;
  }
  diagnostic->tok = tok;
  diagnostic->name = name;
  diagnostic->next_all = context->pending_diagnostics_all;
  context->pending_diagnostics_all = diagnostic;
}

void ps_ctx_emit_deferred_parser_diagnostics_in(
    psx_semantic_context_t *context) {
  if (!context) return;
  deferred_parser_diagnostic_t *rev = NULL;
  while (context->pending_diagnostics_all) {
    deferred_parser_diagnostic_t *diagnostic =
        context->pending_diagnostics_all;
    context->pending_diagnostics_all = diagnostic->next_all;
    diagnostic->next_all = rev;
    rev = diagnostic;
  }
  while (rev) {
    deferred_parser_diagnostic_t *diagnostic = rev;
    rev = diagnostic->next_all;
    diag_emit_tokf_in(
        context->diagnostic_context,
        DIAG_ERR_PARSER_UNSUPPORTED_GNU_EXTENSION, diagnostic->tok,
        diag_message_for_in(
            context->diagnostic_context,
            DIAG_ERR_PARSER_UNSUPPORTED_GNU_EXTENSION),
        diagnostic->name ? diagnostic->name : "");
    ctx_release_in(context, diagnostic);
  }
}

/* タグの完全型定義状態をソフトリセットする。member tableも翻訳単位ごとの情報なので
 * 同時に破棄する。従来はmember recordを残して同名tagの次回parseで上書きしていたため、
 * duplicate判定を正しく行うと前回翻訳単位のmemberを誤検出していた。 */
void ps_ctx_reset_tag_diag_state_in(
    psx_semantic_context_t *context) {
  if (!context || !context->scope_graph) return;
  size_t declaration_count = psx_scope_graph_declaration_count(
      context->scope_graph);
  for (size_t index = 0; index < declaration_count; index++) {
    const psx_scope_declaration_t *declaration =
        psx_scope_graph_declaration_at(context->scope_graph, index);
    if (!declaration || declaration->name_space != PSX_NAMESPACE_TAG ||
        declaration->kind != PSX_DECL_TAG || !declaration->payload)
      continue;
    tag_type_t *t = declaration->payload;
    t->enum_is_complete = 0;
    /* Published record declarations remain valid for canonical types from the
     * previous parse. A later parse starts a new registry-owned generation. */
    t->record_decl = NULL;
    t->record_decl_members = NULL;
    t->member_scope_id = PSX_SCOPE_ID_INVALID;
  }
  context->aggregate_member_layout_drafts = NULL;
}

/* 各 parse 開始時に呼ぶ、関数名テーブルの「ソフトリセット」: 累積状態 (関数情報) は残し、
 * 同一 parse 内でのみ意味を持つ is_defined のみクリアする。これにより同一プロセス内で複数回frontend parseを
 * を呼ぶユニットテストで前回パースの "function defined" 状態が今回パースに漏れない。 */
void ps_ctx_reset_function_diag_state_in(
    psx_semantic_context_t *context) {
  if (!context || !context->scope_graph) return;
  size_t declaration_count = psx_scope_graph_declaration_count(
      context->scope_graph);
  for (size_t index = 0; index < declaration_count; index++) {
    psx_function_symbol_t *function = function_symbol_from_declaration(
        psx_scope_graph_declaration_at(context->scope_graph, index));
    if (function) function->is_defined = 0;
  }
}

static tag_type_t *tag_type_from_declaration_in(
    psx_semantic_context_t *context, psx_decl_id_t declaration_id) {
  const psx_scope_declaration_t *declaration =
      context && context->scope_graph
          ? psx_scope_graph_declaration(
                context->scope_graph, declaration_id)
          : NULL;
  return declaration && declaration->kind == PSX_DECL_TAG
             ? declaration->payload
             : NULL;
}

static tag_type_t *find_visible_tag_by_name_in(
    psx_semantic_context_t *context, char *name, int len) {
  if (!context || !context->scope_graph || !name || len <= 0)
    return NULL;
  psx_decl_id_t declaration_id = psx_scope_graph_lookup(
      context->scope_graph, PSX_NAMESPACE_TAG, name, len,
      psx_scope_graph_capture_lookup_point(context->scope_graph));
  return tag_type_from_declaration_in(context, declaration_id);
}

static int ensure_tag_member_scope_in(
    psx_semantic_context_t *context, tag_type_t *tag) {
  if (!context || !context->scope_graph || !tag ||
      (tag->kind != TK_STRUCT && tag->kind != TK_UNION))
    return 0;
  if (tag->member_scope_id != PSX_SCOPE_ID_INVALID) return 1;
  const psx_scope_declaration_t *declaration =
      tag_declaration_for_payload_in(context, tag);
  if (!declaration) return 0;
  tag->member_scope_id = psx_scope_graph_create_scope_at(
      context->scope_graph, declaration->scope_id, PSX_SCOPE_RECORD);
  return tag->member_scope_id != PSX_SCOPE_ID_INVALID;
}

static tag_type_t *find_tag_type_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len) {
  tag_type_t *tag = find_visible_tag_by_name_in(context, name, len);
  return tag && tag->kind == kind ? tag : NULL;
}

static tag_type_t *find_tag_type_at_scope_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len, int scope_depth) {
  if (!context || !context->scope_graph || !name || len <= 0 ||
      scope_depth < 0)
    return NULL;
  size_t declaration_count = psx_scope_graph_declaration_count(
      context->scope_graph);
  for (size_t index = declaration_count; index > 0; index--) {
    const psx_scope_declaration_t *declaration =
        psx_scope_graph_declaration_at(context->scope_graph, index - 1);
    if (!declaration || declaration->name_space != PSX_NAMESPACE_TAG ||
        declaration->kind != PSX_DECL_TAG ||
        declaration->name_len != len ||
        memcmp(declaration->name, name, (size_t)len) != 0)
      continue;
    tag_type_t *tag = declaration->payload;
    if (tag && tag->kind == kind &&
        psx_scope_graph_scope_depth(
            context->scope_graph, declaration->scope_id) == scope_depth)
      return tag;
  }
  return NULL;
}

static tag_type_t *find_tag_type_by_record_id_in(
    psx_semantic_context_t *context, psx_record_id_t record_id) {
  if (!context || !context->scope_graph ||
      record_id == PSX_RECORD_ID_INVALID)
    return NULL;
  size_t declaration_count = psx_scope_graph_declaration_count(
      context->scope_graph);
  for (size_t index = declaration_count; index > 0; index--) {
    const psx_scope_declaration_t *declaration =
        psx_scope_graph_declaration_at(context->scope_graph, index - 1);
    if (!declaration || declaration->name_space != PSX_NAMESPACE_TAG ||
        declaration->kind != PSX_DECL_TAG || !declaration->payload)
      continue;
    tag_type_t *tag = declaration->payload;
    if (tag->record_decl && tag->record_decl->record_id == record_id)
      return tag;
  }
  return NULL;
}

bool ps_ctx_has_tag_type_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len) {
  return find_tag_type_in(context, kind, name, len) != NULL;
}

psx_type_t *ps_ctx_clone_tag_type_at_in_contexts(
    psx_semantic_context_t *context,
    psx_local_registry_t *local_registry,
    token_kind_t kind, char *name, int len,
    psx_local_lookup_point_t point) {
  if (!context || !local_registry || !name || len <= 0) return NULL;
  if (!context->scope_graph ||
      context->scope_graph !=
          ps_local_registry_scope_graph(local_registry))
    return NULL;
  psx_scope_lookup_point_t graph_point =
      point.scope_seq == PSX_SCOPE_ID_INVALID
          ? psx_scope_graph_capture_lookup_point(context->scope_graph)
          : (psx_scope_lookup_point_t){
                .scope_id = point.scope_seq,
                .declaration_order = point.declaration_seq,
            };
  psx_decl_id_t declaration_id = psx_scope_graph_lookup(
      context->scope_graph, PSX_NAMESPACE_TAG, name, len,
      graph_point);
  tag_type_t *tag = tag_type_from_declaration_in(
      context, declaration_id);
  if (!tag || tag->kind != kind) return NULL;
  if (kind == TK_ENUM) {
    int scope_depth = tag_scope_depth_in(context, tag);
    return ps_type_new_enum_in(
        context->arena_context, name, len,
        scope_depth >= 0 ? scope_depth + 1 : 0);
  }
  return ps_type_new_record_in(
      context->arena_context, tag->record_decl);
}

int ps_ctx_register_tag_type_in_contexts(
    psx_semantic_context_t *context,
    psx_local_registry_t *local_registry,
    token_kind_t kind, char *name, int len,
    int is_complete, int member_count) {
  if (!context || !local_registry || !name || len <= 0) return 0;
  psx_scope_graph_t *scope_graph = context->scope_graph;
  if (scope_graph != ps_local_registry_scope_graph(local_registry))
    return 0;
  tag_type_t *existing = NULL;
  psx_decl_id_t declaration_id = psx_scope_graph_lookup_in_scope(
      scope_graph, psx_scope_graph_current_scope(scope_graph),
      PSX_NAMESPACE_TAG, name, len);
  existing = tag_type_from_declaration_in(context, declaration_id);
  if (existing && existing->kind != kind) return 0;
  if (existing) {
    /* C11 6.7.2.1p1 / 6.7.2.2p2 / 6.7.2.3p3: 同一スコープでの完全型タグの再定義は不可。
     * 既存もメンバを持っている (= 完全型) のに、今回も新しいメンバを持っている (= 完全型) なら
     * 二重定義。一方が前方宣言なら従来どおり update。 */
    if (tag_type_is_complete(existing) && is_complete) return 0;
    if (kind == TK_STRUCT || kind == TK_UNION) {
      if (is_complete &&
          !ensure_tag_member_scope_in(context, existing))
        return 0;
      psx_record_decl_t *record_decl = (psx_record_decl_t *)
          ps_ctx_ensure_tag_record_decl_in(context, kind, name, len);
      if (!record_decl) return 0;
      if (member_count > record_decl->member_count)
        record_decl->member_count = member_count;
      if (is_complete) record_decl->is_complete = 1;
      refresh_cached_record_decl(context, existing);
    } else if (kind == TK_ENUM && is_complete) {
      existing->enum_is_complete = 1;
    }
    return 1;
  }
  tag_type_t *t = ctx_calloc_in(context, 1, sizeof(tag_type_t));
  if (!t) return 0;
  t->kind = kind;
  t->name = name;
  t->len = len;
  t->enum_is_complete = kind == TK_ENUM && is_complete ? 1 : 0;
  t->member_scope_id = PSX_SCOPE_ID_INVALID;
  if (kind == TK_STRUCT || kind == TK_UNION) {
    t->record_decl = ctx_calloc_in(
        context, 1, sizeof(psx_record_decl_t));
    if (!t->record_decl) return 0;
    t->record_decl->record_id = allocate_record_id(context);
    t->record_decl->record_kind = ps_type_kind_from_tag_kind(kind);
    t->record_decl->tag_name = name;
    t->record_decl->tag_len = len;
    t->record_decl->is_complete = is_complete ? 1 : 0;
    t->record_decl->member_count = member_count;
  }
  declaration_id = psx_scope_graph_declare(
      scope_graph, PSX_NAMESPACE_TAG, PSX_DECL_TAG,
      name, len, t);
  const psx_scope_declaration_t *declaration =
      psx_scope_graph_declaration(scope_graph, declaration_id);
  if (!declaration) return 0;
  if (t->record_decl && !psx_record_decl_table_define(
                            context->record_decls, t->record_decl)) {
    psx_scope_graph_forget_declaration(
        scope_graph, declaration_id);
    return 0;
  }
  if (is_complete && t->record_decl &&
      !ensure_tag_member_scope_in(context, t)) {
    psx_scope_graph_forget_declaration(
        scope_graph, declaration_id);
    return 0;
  }
  refresh_cached_record_decl(context, t);
  return 1;
}

int ps_ctx_current_tag_scope_depth_in(psx_semantic_context_t *context) {
  return context && context->scope_graph
             ? psx_scope_graph_scope_depth(
                   context->scope_graph,
                   psx_scope_graph_current_scope(context->scope_graph))
             : -1;
}

int ps_ctx_find_tag_kind_at_current_scope_in(
    psx_semantic_context_t *context,
    char *name, int len, token_kind_t *out_kind) {
  if (!context || !context->scope_graph || !name || len <= 0) return 0;
  psx_decl_id_t declaration_id = psx_scope_graph_lookup_in_scope(
      context->scope_graph,
      psx_scope_graph_current_scope(context->scope_graph),
      PSX_NAMESPACE_TAG, name, len);
  tag_type_t *tag = tag_type_from_declaration_in(
      context, declaration_id);
  if (!tag) return 0;
  if (out_kind) *out_kind = tag->kind;
  return 1;
}

int ps_ctx_get_tag_member_count_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len) {
  tag_type_t *t = find_tag_type_in(context, kind, name, len);
  return t && t->record_decl ? t->record_decl->member_count : -1;
}

const psx_record_decl_t *ps_ctx_ensure_tag_record_decl_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len) {
  tag_type_t *tag = find_tag_type_in(context, kind, name, len);
  if (!tag || (kind != TK_STRUCT && kind != TK_UNION)) return NULL;
  if (tag->record_decl) return tag->record_decl;

  psx_record_decl_t *record_decl =
      ctx_calloc_in(context, 1, sizeof(psx_record_decl_t));
  if (!record_decl) return NULL;
  tag->record_decl = record_decl;
  record_decl->record_id = allocate_record_id(context);
  record_decl->record_kind = ps_type_kind_from_tag_kind(kind);
  record_decl->tag_name = name;
  record_decl->tag_len = len;
  if (record_decl->record_id != PSX_RECORD_ID_INVALID &&
      !psx_record_decl_table_define(context->record_decls, record_decl))
    return NULL;
  refresh_cached_record_decl(context, tag);
  return record_decl;
}

psx_record_id_t ps_ctx_resolve_tag_record_id_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len) {
  const psx_record_decl_t *record =
      ps_ctx_ensure_tag_record_decl_in(context, kind, name, len);
  return record ? record->record_id : PSX_RECORD_ID_INVALID;
}

const psx_record_decl_t *ps_ctx_get_record_decl_in(
    psx_semantic_context_t *context, psx_record_id_t record_id) {
  return psx_record_decl_table_lookup(
      ps_ctx_record_decl_table_in(context), record_id);
}

int ps_ctx_publish_record_layout_in(
    psx_semantic_context_t *context, psx_record_id_t record_id,
    int size, int alignment) {
  const psx_record_decl_t *record = ps_ctx_get_record_decl_in(
      context, record_id);
  tag_type_t *tag = find_tag_type_by_record_id_in(context, record_id);
  if (!context || !record || !record->is_complete || size < 0 ||
      alignment <= 0 || record->member_count < 0 || !tag)
    return 0;
  psx_record_member_layout_t *members = NULL;
  tag_member_t **source_members = NULL;
  int source_member_count = collect_tag_member_declarations_in(
      context, tag, &source_members);
  if (source_member_count < 0 || source_member_count != record->member_count) {
    free(source_members);
    return 0;
  }
  if (source_member_count > 0) {
    members = malloc((size_t)record->member_count * sizeof(*members));
    if (!members) {
      free(source_members);
      return 0;
    }
    for (int i = 0; i < record->member_count; i++) {
      const psx_record_member_layout_t *layout =
          find_tag_member_layout_draft(context, source_members[i]);
      if (!layout) {
        free(source_members);
        free(members);
        return 0;
      }
      members[i] = *layout;
    }
  }
  int published = psx_record_layout_table_define(
      context->record_layouts, record_id, ps_ctx_data_layout(context), size,
      alignment, members, record->member_count);
  free(source_members);
  free(members);
  return published;
}

int ps_ctx_get_tag_size_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len) {
  tag_type_t *t = find_tag_type_in(context, kind, name, len);
  if (!t) return -1;
  if (kind == TK_ENUM)
    return ag_data_layout_scalar_size(ps_ctx_data_layout(context),
                                      AG_TARGET_SCALAR_INT);
  if (!t->record_decl || t->record_decl->record_id == PSX_RECORD_ID_INVALID)
    return 0;
  const psx_record_layout_t *layout = psx_record_layout_table_lookup(
      context->record_layouts, t->record_decl->record_id,
      ps_ctx_data_layout(context));
  return layout ? layout->size : 0;
}

int ps_ctx_get_tag_align_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len) {
  tag_type_t *t = find_tag_type_in(context, kind, name, len);
  if (!t) return -1;
  if (kind == TK_ENUM)
    return ag_data_layout_scalar_alignment(ps_ctx_data_layout(context),
                                           AG_TARGET_SCALAR_INT);
  if (!t->record_decl || t->record_decl->record_id == PSX_RECORD_ID_INVALID)
    return -1;
  const psx_record_layout_t *layout = psx_record_layout_table_lookup(
      context->record_layouts, t->record_decl->record_id,
      ps_ctx_data_layout(context));
  return layout ? layout->alignment : -1;
}

static tag_member_t *find_tag_member_record_in(
    psx_semantic_context_t *context, tag_type_t *tag,
    const psx_record_member_decl_t *declaration) {
  if (!context || !tag || !declaration || declaration->len <= 0)
    return NULL;
  if (!context->scope_graph ||
      tag->member_scope_id == PSX_SCOPE_ID_INVALID)
    return NULL;
  psx_decl_id_t declaration_id = psx_scope_graph_lookup_in_scope(
      context->scope_graph, tag->member_scope_id,
      PSX_NAMESPACE_MEMBER, declaration->name, declaration->len);
  const psx_scope_declaration_t *binding =
      psx_scope_graph_declaration(
          context->scope_graph, declaration_id);
  return binding && binding->kind == PSX_DECL_MEMBER
             ? binding->payload
             : NULL;
}

static int insert_tag_member_record_in(
    psx_semantic_context_t *context, tag_type_t *tag,
    const psx_record_member_decl_t *declaration,
    const psx_record_member_layout_t *layout) {
  if (!context || !tag || !declaration) return 0;
  if (!context->scope_graph ||
      tag->member_scope_id == PSX_SCOPE_ID_INVALID)
    return 0;
  tag_member_t *m = ctx_calloc_in(context, 1, sizeof(tag_member_t));
  if (!m) return 0;
  m->declaration.name = declaration->name;
  m->declaration.len = declaration->len;
  if (!initialize_tag_member_record(context, m, declaration, layout)) return 0;
  return psx_scope_graph_declare_at(
             context->scope_graph, tag->member_scope_id,
             PSX_NAMESPACE_MEMBER, PSX_DECL_MEMBER,
             declaration->len > 0 ? declaration->name : NULL,
             declaration->len, m) != PSX_DECL_ID_INVALID;
}

static int count_tag_member_records_in(
  const psx_semantic_context_t *context, const tag_type_t *tag) {
  if (!context || !context->scope_graph || !tag ||
      tag->member_scope_id == PSX_SCOPE_ID_INVALID)
    return 0;
  int count = 0;
  size_t declaration_count = psx_scope_graph_declaration_count(
      context->scope_graph);
  for (size_t index = 0; index < declaration_count; index++) {
    const psx_scope_declaration_t *declaration =
        psx_scope_graph_declaration_at(context->scope_graph, index);
    if (declaration && declaration->scope_id == tag->member_scope_id &&
        declaration->name_space == PSX_NAMESPACE_MEMBER &&
        declaration->kind == PSX_DECL_MEMBER)
      count++;
  }
  return count;
}

int psx_ctx_register_tag_member_in(
    psx_semantic_context_t *context,
    token_kind_t tag_kind, char *tag_name, int tag_len,
    const psx_record_member_decl_t *declaration,
    const psx_record_member_layout_t *layout, int *out_created) {
  if (out_created) *out_created = 0;
  if (!ps_ctx_register_tag_members_in(
          context, tag_kind, tag_name, tag_len,
          declaration, layout, 1, NULL))
    return 0;
  if (out_created) *out_created = 1;
  return 1;
}

static int register_tag_members_for_owner_in(
    psx_semantic_context_t *context, tag_type_t *tag,
    const psx_record_member_decl_t *declarations,
    const psx_record_member_layout_t *layouts, int member_count,
    int *out_conflict_index) {
  if (out_conflict_index) *out_conflict_index = -1;
  if (!context || !tag ||
      (tag->kind != TK_STRUCT && tag->kind != TK_UNION) ||
      !declarations || !layouts || member_count <= 0) {
    return 0;
  }
  if (!ensure_tag_member_scope_in(context, tag)) return 0;

  for (int i = 0; i < member_count; i++) {
    const psx_record_member_decl_t *declaration = &declarations[i];
    const psx_record_member_layout_t *layout = &layouts[i];
    if (!declaration->name || declaration->len < 0 ||
        !psx_record_member_decl_type(declaration) || layout->offset < 0 ||
        layout->bit_offset < 0) {
      if (out_conflict_index) *out_conflict_index = i;
      return 0;
    }
    if (declaration->len == 0) continue;
    if (find_tag_member_record_in(context, tag, declaration)) {
      if (out_conflict_index) *out_conflict_index = i;
      return 0;
    }
    for (int j = 0; j < i; j++) {
      if (declarations[j].len == declaration->len &&
          declaration->len > 0 &&
          strncmp(declarations[j].name, declaration->name,
                  (size_t)declaration->len) == 0) {
        if (out_conflict_index) *out_conflict_index = i;
        return 0;
      }
    }
  }

  for (int i = 0; i < member_count; i++) {
    const psx_record_member_decl_t *declaration = &declarations[i];
    if (!insert_tag_member_record_in(
            context, tag, declaration, &layouts[i]))
      return 0;
  }
  if (tag && tag->record_decl) {
    tag->record_decl->member_count =
        count_tag_member_records_in(context, tag);
    refresh_cached_record_decl(context, tag);
    const psx_record_layout_t *layout = psx_record_layout_table_lookup(
        context->record_layouts, tag->record_decl->record_id,
        ps_ctx_data_layout(context));
    if (tag->record_decl->is_complete && layout) {
      int size = layout->size;
      int alignment = layout->alignment;
      (void)ps_ctx_publish_record_layout_in(
          context, tag->record_decl->record_id,
          size, alignment);
    }
  }
  return 1;
}

int ps_ctx_register_tag_members_in(
    psx_semantic_context_t *context,
    token_kind_t tag_kind, char *tag_name, int tag_len,
    const psx_record_member_decl_t *declarations,
    const psx_record_member_layout_t *layouts, int member_count,
    int *out_conflict_index) {
  if (!context ||
      (tag_kind != TK_STRUCT && tag_kind != TK_UNION) || !tag_name ||
      tag_len <= 0) {
    if (out_conflict_index) *out_conflict_index = -1;
    return 0;
  }
  return register_tag_members_for_owner_in(
      context, find_tag_type_in(context, tag_kind, tag_name, tag_len),
      declarations, layouts, member_count, out_conflict_index);
}

int ps_ctx_register_record_members_in(
    psx_semantic_context_t *context, psx_record_id_t record_id,
    const psx_record_member_decl_t *declarations,
    const psx_record_member_layout_t *layouts, int member_count,
    int *out_conflict_index) {
  tag_type_t *tag = find_tag_type_by_record_id_in(context, record_id);
  if (!tag) {
    if (out_conflict_index) *out_conflict_index = -1;
    return 0;
  }
  return register_tag_members_for_owner_in(
      context, tag,
      declarations, layouts, member_count, out_conflict_index);
}

/* Resolve aggregate identity across the complete owned type tree. Record
 * declarations remain owned by RecordDeclTable and are not retained by types. */
void ps_ctx_bind_record_ids_in(
    psx_semantic_context_t *context, psx_type_t *type) {
  if (!context || !type) return;
  if (ps_type_is_tag_aggregate(type)) {
    psx_record_id_t record_id = type->record_id;
    if (record_id == PSX_RECORD_ID_INVALID && type->tag_name &&
        type->tag_len > 0) {
      record_id = ps_ctx_resolve_tag_record_id_in(
          context, ps_type_tag_token_kind(type),
          type->tag_name, type->tag_len);
    }
    type->record_id = record_id;
  }
  ps_ctx_bind_record_ids_in(context, psx_type_owned_base_mut(type));
  for (int i = 0; i < type->param_count; i++)
    ps_ctx_bind_record_ids_in(
        context, psx_type_owned_param_mut(type, i));
}

static bool fill_tag_member_in(
    psx_semantic_context_t *context,
    tag_member_t *member,
    psx_record_member_decl_t *out_declaration,
    psx_record_member_layout_t *out_layout) {
  if (!context || !member || (!out_declaration && !out_layout)) return false;
  const psx_record_member_layout_t *layout =
      find_tag_member_layout_draft(context, member);
  if (!layout) return false;
  if (out_declaration) {
    *out_declaration = (psx_record_member_decl_t){
        .name = member->declaration.name,
        .len = member->declaration.len,
        .bit_width = member->declaration.bit_width,
        .bit_is_signed = member->declaration.bit_is_signed,
        .decl_type_table = member->declaration.type_table,
        .decl_qual_type = member->declaration.qual_type,
    };
  }
  if (out_layout) *out_layout = *layout;
  return true;
}

/* 内部実装: scope_depth が指定 (>=0) ならその深度に固定、負なら find_tag_type の
 * 最も内側 tag の scope_depth を使う。 */
static bool get_tag_member_impl_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len,
    int scope_depth, int index,
    psx_record_member_decl_t *out_declaration,
    psx_record_member_layout_t *out_layout) {
  if (!context || (!out_declaration && !out_layout)) return false;
  tag_type_t *tag = scope_depth >= 0
                        ? find_tag_type_at_scope_in(
                              context, kind, name, len, scope_depth)
                        : find_tag_type_in(context, kind, name, len);
  if (!tag) return false;
  tag_member_t **members = NULL;
  int n = collect_tag_member_declarations_in(context, tag, &members);
  if (n <= 0 || index < 0 || index >= n) {
    free(members);
    return false;
  }
  bool found = fill_tag_member_in(
      context, members[index], out_declaration, out_layout);
  free(members);
  return found;
}

static bool find_tag_member_impl_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len, int scope_depth,
    char *member_name, int member_len,
    psx_record_member_decl_t *out_declaration,
    psx_record_member_layout_t *out_layout) {
  if (!context || (!out_declaration && !out_layout)) return false;
  tag_type_t *tag = scope_depth >= 0
                        ? find_tag_type_at_scope_in(
                              context, kind, name, len, scope_depth)
                        : find_tag_type_in(context, kind, name, len);
  if (!tag) return false;
  if (member_len <= 0 || !context->scope_graph ||
      tag->member_scope_id == PSX_SCOPE_ID_INVALID)
    return false;
  psx_decl_id_t declaration_id = psx_scope_graph_lookup_in_scope(
      context->scope_graph, tag->member_scope_id,
      PSX_NAMESPACE_MEMBER, member_name, member_len);
  const psx_scope_declaration_t *binding =
      psx_scope_graph_declaration(
          context->scope_graph, declaration_id);
  tag_member_t *member =
      binding && binding->kind == PSX_DECL_MEMBER
          ? binding->payload
          : NULL;
  return member && fill_tag_member_in(
             context, member, out_declaration, out_layout);
}

bool ps_ctx_get_tag_member_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len, int index,
    psx_record_member_decl_t *out_declaration,
    psx_record_member_layout_t *out_layout) {
  return get_tag_member_impl_in(
      context, kind, name, len, -1, index,
      out_declaration, out_layout);
}

bool ps_ctx_get_tag_member_at_scope_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len,
    int scope_depth, int index,
    psx_record_member_decl_t *out_declaration,
    psx_record_member_layout_t *out_layout) {
  return get_tag_member_impl_in(
      context, kind, name, len, scope_depth, index,
      out_declaration, out_layout);
}

bool ps_ctx_find_tag_member_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len,
    char *member_name, int member_len,
    psx_record_member_decl_t *out_declaration,
    psx_record_member_layout_t *out_layout) {
  return find_tag_member_impl_in(
      context, kind, name, len, -1, member_name, member_len,
      out_declaration, out_layout);
}

bool ps_ctx_find_tag_member_at_scope_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len, int scope_depth,
    char *member_name, int member_len,
    psx_record_member_decl_t *out_declaration,
    psx_record_member_layout_t *out_layout) {
  return find_tag_member_impl_in(
      context, kind, name, len, scope_depth, member_name, member_len,
      out_declaration, out_layout);
}

int ps_ctx_get_tag_scope_depth_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len) {
  tag_type_t *t = find_tag_type_in(context, kind, name, len);
  return tag_scope_depth_in(context, t);
}

void ps_ctx_promote_tag_to_file_scope_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len) {
  tag_type_t *t = find_tag_type_in(context, kind, name, len);
  const psx_scope_declaration_t *declaration =
      tag_declaration_for_payload_in(context, t);
  if (!declaration || declaration->scope_id == PSX_SCOPE_ID_TRANSLATION_UNIT)
    return;
  psx_scope_graph_rehome_declaration_at(
      context->scope_graph, declaration->id,
      PSX_SCOPE_ID_TRANSLATION_UNIT);
}

int ps_ctx_get_tag_member_count_at_scope_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len, int scope_depth) {
  tag_type_t *tag = find_tag_type_at_scope_in(
      context, kind, name, len, scope_depth);
  return tag && tag->record_decl ? tag->record_decl->member_count : -1;
}

// 任意のスコープから名前一致の enum_const を返す。なければ NULL。
static enum_const_t *find_enum_const_in(
    psx_semantic_context_t *context, char *name, int len) {
  if (!context || !context->scope_graph || !name || len <= 0) return NULL;
  psx_decl_id_t id = psx_scope_graph_lookup(
      context->scope_graph, PSX_NAMESPACE_ORDINARY, name, len,
      psx_scope_graph_capture_lookup_point(context->scope_graph));
  const psx_scope_declaration_t *declaration =
      psx_scope_graph_declaration(context->scope_graph, id);
  return declaration && declaration->kind == PSX_DECL_ENUM_CONSTANT
             ? declaration->payload
             : NULL;
}

/* enum 定数を登録する。
 * 戻り値: 1 = 新規登録に成功、0 = 同名定数が既に同スコープにあった (重複)。
 * 重複時はテーブルを変更しない (呼び出し元で診断を出す)。 */
int ps_ctx_register_enum_const_in_contexts(
    psx_semantic_context_t *context,
    psx_local_registry_t *local_registry,
    char *name, int len, long long value, int *out_created) {
  if (out_created) *out_created = 0;
  if (!context || !local_registry || !name || len <= 0) return 0;
  psx_scope_graph_t *scope_graph = context->scope_graph;
  if (!scope_graph ||
      scope_graph != ps_local_registry_scope_graph(local_registry))
    return 0;
  if (psx_scope_graph_lookup_in_scope(
          scope_graph, psx_scope_graph_current_scope(scope_graph),
          PSX_NAMESPACE_ORDINARY, name, len) != PSX_DECL_ID_INVALID) {
    return 0;
  }
  enum_const_t *e = ctx_calloc_in(context, 1, sizeof(enum_const_t));
  if (!e) return 0;
  e->value = value;
  psx_decl_id_t declaration_id = psx_scope_graph_declare(
      scope_graph, PSX_NAMESPACE_ORDINARY,
      PSX_DECL_ENUM_CONSTANT, name, len, e);
  if (declaration_id == PSX_DECL_ID_INVALID) return 0;
  if (out_created) *out_created = 1;
  return 1;
}

bool ps_ctx_find_enum_const_in(
    psx_semantic_context_t *context,
    char *name, int len, long long *out_value) {
  enum_const_t *e = find_enum_const_in(context, name, len);
  if (!e) return false;
  if (out_value) *out_value = e->value;
  return true;
}

bool ps_ctx_enum_const_value_by_declaration_id_in(
    psx_semantic_context_t *context, psx_decl_id_t declaration_id,
    long long *out_value) {
  if (!context || !context->scope_graph) return false;
  const psx_scope_declaration_t *declaration =
      psx_scope_graph_declaration(
          context->scope_graph, declaration_id);
  if (!declaration || declaration->kind != PSX_DECL_ENUM_CONSTANT ||
      !declaration->payload)
    return false;
  const enum_const_t *entry = declaration->payload;
  if (out_value) *out_value = entry->value;
  return true;
}

bool ps_ctx_find_enum_const_at_in_contexts(
    psx_semantic_context_t *context,
    psx_local_registry_t *local_registry,
    char *name, int len, psx_local_lookup_point_t point,
    long long *out_value) {
  if (!context || !local_registry || !name || len <= 0) return false;
  psx_scope_graph_t *scope_graph = context->scope_graph;
  if (!scope_graph ||
      scope_graph != ps_local_registry_scope_graph(local_registry))
    return false;
  psx_scope_lookup_point_t graph_point =
      point.scope_seq == PSX_SCOPE_ID_INVALID
          ? psx_scope_graph_capture_lookup_point(scope_graph)
          : (psx_scope_lookup_point_t){
                .scope_id = point.scope_seq,
                .declaration_order = point.declaration_seq,
            };
  psx_decl_id_t id = psx_scope_graph_lookup(
      scope_graph, PSX_NAMESPACE_ORDINARY, name, len, graph_point);
  const psx_scope_declaration_t *declaration =
      psx_scope_graph_declaration(scope_graph, id);
  if (!declaration || declaration->kind != PSX_DECL_ENUM_CONSTANT)
    return false;
  enum_const_t *entry = declaration->payload;
  if (out_value) *out_value = entry->value;
  return true;
}

static typedef_name_t *find_typedef_in(
    psx_semantic_context_t *context, char *name, int len) {
  if (!context || !context->scope_graph || !name || len <= 0) return NULL;
  psx_decl_id_t id = psx_scope_graph_lookup(
      context->scope_graph, PSX_NAMESPACE_ORDINARY, name, len,
      psx_scope_graph_capture_lookup_point(context->scope_graph));
  const psx_scope_declaration_t *declaration =
      psx_scope_graph_declaration(context->scope_graph, id);
  return declaration && declaration->kind == PSX_DECL_TYPEDEF
             ? declaration->payload
             : NULL;
}

static psx_runtime_declarator_application_t *
clone_typedef_runtime_application(
    psx_semantic_context_t *context,
    const psx_runtime_declarator_application_t *source) {
  if (!source) return NULL;
  if (!context || source->shape.count < 0 ||
      source->array_bound_count < 0 ||
      (source->shape.count > 0 && !source->shape.ops) ||
      (source->array_bound_count > 0 && !source->array_bounds))
    return NULL;
  psx_runtime_declarator_application_t *copy = ctx_calloc_in(
      context, 1, sizeof(*copy));
  if (!copy) return NULL;
  if (source->shape.count > 0) {
    copy->shape.ops = ctx_calloc_in(
        context, (size_t)source->shape.count,
        sizeof(*copy->shape.ops));
    if (!copy->shape.ops) return NULL;
    memcpy(
        copy->shape.ops, source->shape.ops,
        (size_t)source->shape.count * sizeof(*copy->shape.ops));
    copy->shape.count = source->shape.count;
    copy->shape.capacity = source->shape.count;
  }
  if (source->array_bound_count > 0) {
    copy->array_bounds = ctx_calloc_in(
        context, (size_t)source->array_bound_count,
        sizeof(*copy->array_bounds));
    if (!copy->array_bounds) return NULL;
    memcpy(
        copy->array_bounds, source->array_bounds,
        (size_t)source->array_bound_count *
            sizeof(*copy->array_bounds));
    copy->array_bound_count = source->array_bound_count;
  }
  return copy;
}

psx_qual_type_t ps_ctx_intern_declaration_qual_type_in(
    psx_semantic_context_t *context, const psx_type_t *type) {
  if (!context || !type)
    return (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                             PSX_TYPE_QUALIFIER_NONE};
  psx_type_t *resolved_type = ctx_type_clone_persistent_in(
      context, type);
  if (!resolved_type)
    return (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                             PSX_TYPE_QUALIFIER_NONE};
  ps_ctx_bind_record_ids_in(context, resolved_type);
  return ps_ctx_intern_qual_type_in(context, resolved_type);
}

static psx_qual_type_t resolve_typedef_decl_qual_type(
    psx_semantic_context_t *context,
    const psx_typedef_info_t *info) {
  psx_qual_type_t identity = ps_ctx_typedef_decl_qual_type(info);
  if (!context || !info ||
      info->decl_type_table != context->semantic_types ||
      identity.type_id == PSX_TYPE_ID_INVALID ||
      !psx_semantic_type_table_lookup_qual_type(
          context->semantic_types, identity))
    return (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                             PSX_TYPE_QUALIFIER_NONE};
  return identity;
}

static int initialize_typedef_record(
    psx_semantic_context_t *context,
    typedef_name_t *t, const psx_typedef_info_t *info,
    psx_qual_type_t identity) {
  if (!t || !info ||
      t->decl_qual_type.type_id != PSX_TYPE_ID_INVALID ||
      identity.type_id == PSX_TYPE_ID_INVALID)
    return 0;
  t->decl_qual_type = identity;
  if (info->runtime_application) {
    t->runtime_application = clone_typedef_runtime_application(
        context, info->runtime_application);
    if (!t->runtime_application) return 0;
  }
  return 1;
}

int ps_ctx_register_typedef_name_in_contexts(
    psx_semantic_context_t *context,
    psx_local_registry_t *local_registry,
    char *name, int len, const psx_typedef_info_t *info,
    int *out_created, int *out_redeclared) {
  if (out_created) *out_created = 0;
  if (out_redeclared) *out_redeclared = 0;
  if (!context || !local_registry || !name || len <= 0 || !info)
    return 0;
  psx_scope_graph_t *scope_graph = context->scope_graph;
  if (!scope_graph ||
      scope_graph != ps_local_registry_scope_graph(local_registry))
    return 0;
  psx_qual_type_t identity = resolve_typedef_decl_qual_type(
      context, info);
  if (identity.type_id == PSX_TYPE_ID_INVALID) return 0;
  typedef_name_t *existing = NULL;
  psx_decl_id_t id = psx_scope_graph_lookup_in_scope(
      scope_graph, psx_scope_graph_current_scope(scope_graph),
      PSX_NAMESPACE_ORDINARY, name, len);
  const psx_scope_declaration_t *declaration =
      psx_scope_graph_declaration(scope_graph, id);
  if (declaration && declaration->kind != PSX_DECL_TYPEDEF) return 0;
  if (declaration) existing = declaration->payload;
  /* C11 6.7p3: typedef は同じ型なら再宣言可。違う型なら error。
   * canonical type identity が一致する場合だけ同じ宣言として扱う。 */
  if (existing) {
    psx_qual_type_t existing_identity =
        typedef_record_decl_qual_type(existing);
    if (existing_identity.type_id != identity.type_id ||
        existing_identity.qualifiers != identity.qualifiers)
      return 0;
    if (out_redeclared) *out_redeclared = 1;
    return 1;  /* 同じ型なら登録済みのままで OK */
  }
  typedef_name_t *t = ctx_calloc_in(
      context, 1, sizeof(typedef_name_t));
  if (!t) return 0;
  if (!initialize_typedef_record(context, t, info, identity)) return 0;
  psx_decl_id_t declaration_id = psx_scope_graph_declare(
      scope_graph, PSX_NAMESPACE_ORDINARY,
      PSX_DECL_TYPEDEF, name, len, t);
  if (declaration_id == PSX_DECL_ID_INVALID) return 0;
  if (out_created) *out_created = 1;
  return 1;
}

bool psx_ctx_find_typedef_layout_in(
    psx_semantic_context_t *context,
    char *name, int len, int *out_size, int *out_alignment) {
  typedef_name_t *t = find_typedef_in(context, name, len);
  if (!t) return false;
  psx_qual_type_t qual_type = typedef_record_decl_qual_type(t);
  if (out_size)
    *out_size =
        ps_type_sizeof_id(context->semantic_types, context->record_layouts,
                          qual_type.type_id, ps_ctx_data_layout(context));
  if (out_alignment)
    *out_alignment =
        ps_type_alignof_id(context->semantic_types, context->record_layouts,
                           qual_type.type_id, ps_ctx_data_layout(context));
  return true;
}

bool ps_ctx_find_typedef_name_in(
    psx_semantic_context_t *context,
    char *name, int len, psx_typedef_info_t *out) {
  typedef_name_t *t = find_typedef_in(context, name, len);
  if (!t) return false;
  if (out) {
    memset(out, 0, sizeof(*out));
    out->decl_type_table = context->semantic_types;
    out->decl_qual_type = typedef_record_decl_qual_type(t);
    out->runtime_application =
        typedef_record_runtime_application(t);
  }
  return true;
}

bool ps_ctx_find_typedef_decl_type_in(
    psx_semantic_context_t *context,
    char *name, int len, const psx_type_t **out_type) {
  typedef_name_t *t = find_typedef_in(context, name, len);
  if (!t) return false;
  if (out_type) *out_type = typedef_record_decl_type(context, t);
  return true;
}

bool ps_ctx_find_typedef_decl_type_at_in_contexts(
    psx_semantic_context_t *context,
    psx_local_registry_t *local_registry,
    char *name, int len, psx_local_lookup_point_t point,
    const psx_type_t **out_type) {
  psx_typedef_info_t info;
  if (!ps_ctx_find_typedef_name_at_in_contexts(
          context, local_registry, name, len, point, &info))
    return false;
  if (out_type) *out_type = ps_ctx_typedef_decl_type(&info);
  return true;
}

bool ps_ctx_find_typedef_name_at_in_contexts(
    psx_semantic_context_t *context,
    psx_local_registry_t *local_registry,
    char *name, int len, psx_local_lookup_point_t point,
    psx_typedef_info_t *out) {
  if (!context || !local_registry || !name || len <= 0) return false;
  psx_scope_graph_t *scope_graph = context->scope_graph;
  if (!scope_graph ||
      scope_graph != ps_local_registry_scope_graph(local_registry))
    return false;
  psx_scope_lookup_point_t graph_point =
      point.scope_seq == PSX_SCOPE_ID_INVALID
          ? psx_scope_graph_capture_lookup_point(scope_graph)
          : (psx_scope_lookup_point_t){
                .scope_id = point.scope_seq,
                .declaration_order = point.declaration_seq,
            };
  psx_decl_id_t id = psx_scope_graph_lookup(
      scope_graph, PSX_NAMESPACE_ORDINARY, name, len, graph_point);
  const psx_scope_declaration_t *declaration =
      psx_scope_graph_declaration(scope_graph, id);
  if (!declaration || declaration->kind != PSX_DECL_TYPEDEF)
    return false;
  typedef_name_t *type = declaration->payload;
  if (out) {
    *out = (psx_typedef_info_t){
        .decl_type_table = context->semantic_types,
        .decl_qual_type = typedef_record_decl_qual_type(type),
        .runtime_application =
            typedef_record_runtime_application(type),
    };
  }
  return true;
}

bool psx_ctx_is_typedef_name_token_in(
    psx_semantic_context_t *context, token_t *tok) {
  if (!tok || tok->kind != TK_IDENT) return false;
  token_ident_t *id = (token_ident_t *)tok;
  return ps_ctx_find_typedef_name_in(
      context, id->str, id->len, NULL);
}

static int semantic_context_classifies_typedef_name(
    void *context, const token_t *token) {
  return psx_ctx_is_typedef_name_token_in(
      context, (token_t *)token);
}

psx_name_classifier_t ps_ctx_name_classifier(
    psx_semantic_context_t *context) {
  return (psx_name_classifier_t){
      .context = context,
      .is_typedef_name = semantic_context_classifies_typedef_name,
  };
}

// 任意のスコープから名前一致の関数名エントリを返す。なければ NULL。
const psx_function_symbol_t *ps_ctx_find_function_symbol_in(
    psx_semantic_context_t *context, char *name, int len) {
  if (!context || !context->scope_graph || !name || len <= 0) return NULL;
  psx_decl_id_t id = psx_scope_graph_lookup_in_scope(
      context->scope_graph, PSX_SCOPE_ID_TRANSLATION_UNIT,
      PSX_NAMESPACE_ORDINARY, name, len);
  const psx_scope_declaration_t *declaration =
      psx_scope_graph_declaration(context->scope_graph, id);
  return function_symbol_from_declaration(declaration);
}

static psx_function_symbol_t *find_function_name_mut_in(
    psx_semantic_context_t *context, char *name, int len) {
  return (psx_function_symbol_t *)ps_ctx_find_function_symbol_in(
      context, name, len);
}

psx_qual_type_t ps_function_symbol_qual_type(
    const psx_function_symbol_t *symbol) {
  return symbol
             ? symbol->function_qual_type
             : (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                                 PSX_TYPE_QUALIFIER_NONE};
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
  checkpoint->function_qual_type = function->function_qual_type;
}

void ps_ctx_rollback_function_registration_in(
    psx_semantic_context_t *context, char *name, int len,
    const psx_function_registration_checkpoint_t *checkpoint) {
  if (!context || !context->scope_graph || !checkpoint ||
      !name || len <= 0)
    return;
  psx_decl_id_t declaration_id = psx_scope_graph_lookup_in_scope(
      context->scope_graph, PSX_SCOPE_ID_TRANSLATION_UNIT,
      PSX_NAMESPACE_ORDINARY, name, len);
  const psx_scope_declaration_t *declaration =
      psx_scope_graph_declaration(context->scope_graph, declaration_id);
  psx_function_symbol_t *function =
      function_symbol_from_declaration(declaration);
  if (!function) return;
  if (!checkpoint->existed) {
    psx_scope_graph_forget_declaration(
        context->scope_graph, declaration_id);
    ctx_release_in(context, function);
    return;
  }
  function->is_defined = checkpoint->is_defined;
  function->function_qual_type = checkpoint->function_qual_type;
}

static void define_function_name_in(
    psx_semantic_context_t *context, char *name, int len) {
  if (!context || !context->scope_graph || !name || len <= 0) return;
  psx_function_symbol_t *existing =
      find_function_name_mut_in(context, name, len);
  if (existing) return;
  if (psx_scope_graph_lookup_in_scope(
          context->scope_graph, PSX_SCOPE_ID_TRANSLATION_UNIT,
          PSX_NAMESPACE_ORDINARY, name, len) != PSX_DECL_ID_INVALID)
    return;
  psx_function_symbol_t *f =
      ctx_calloc_in(context, 1, sizeof(*f));
  if (!f) return;
  psx_decl_id_t declaration_id = psx_scope_graph_declare_at(
      context->scope_graph, PSX_SCOPE_ID_TRANSLATION_UNIT,
      PSX_NAMESPACE_ORDINARY, PSX_DECL_FUNCTION,
      name, len, f);
  if (declaration_id == PSX_DECL_ID_INVALID) {
    ctx_release_in(context, f);
  }
}

void psx_ctx_define_function_name_in(
    psx_semantic_context_t *context, char *name, int len) {
  define_function_name_in(context, name, len);
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

int ps_ctx_is_function_defined_in(
    psx_semantic_context_t *context, char *name, int len) {
  psx_function_symbol_t *f = find_function_name_mut_in(context, name, len);
  return f && f->is_defined;
}

const psx_type_t *psx_ctx_get_function_ret_type_in(
    psx_semantic_context_t *context, char *name, int len) {
  psx_qual_type_t return_type =
      psx_ctx_get_function_return_qual_type_in(context, name, len);
  return psx_semantic_type_table_lookup_qual_type(
      context ? context->semantic_types : NULL, return_type);
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
    define_function_name_in(context, name, len);
    f = find_function_name_mut_in(context, name, len);
  }
  if (!f) return NULL;
  psx_qual_type_t identity = ps_ctx_intern_qual_type_in(
      context, function_type);
  if (identity.type_id == PSX_TYPE_ID_INVALID) return NULL;
  if (f->function_qual_type.type_id != PSX_TYPE_ID_INVALID) {
    const psx_type_t *existing =
        psx_semantic_type_table_lookup_qual_type(
            context->semantic_types, f->function_qual_type);
    return ps_type_shape_matches(existing, function_type) ? f : NULL;
  }
  f->function_qual_type = identity;
  return f;
}

int psx_ctx_track_function_type_in(
    psx_semantic_context_t *context, char *name, int len,
    const psx_type_t *function_type) {
  return ps_ctx_register_function_type_in(
             context, name, len, function_type) != NULL;
}

const psx_type_t *ps_ctx_get_function_type_in(
    psx_semantic_context_t *context, char *name, int len) {
  return psx_semantic_type_table_lookup_qual_type(
      context ? context->semantic_types : NULL,
      ps_ctx_get_function_qual_type_in(context, name, len));
}

psx_qual_type_t ps_ctx_get_function_qual_type_in(
    psx_semantic_context_t *context, char *name, int len) {
  return ps_function_symbol_qual_type(
      ps_ctx_find_function_symbol_in(context, name, len));
}

psx_qual_type_t psx_ctx_get_function_return_qual_type_in(
    psx_semantic_context_t *context, char *name, int len) {
  psx_qual_type_t function_type =
      ps_ctx_get_function_qual_type_in(context, name, len);
  return psx_semantic_type_table_base(
      context ? context->semantic_types : NULL, function_type.type_id);
}

int ps_ctx_format_function_signature_in(
    psx_semantic_context_t *context, char *name, int len,
    char *out, size_t out_size) {
  const psx_type_t *type = ps_ctx_get_function_type_in(context, name, len);
  if (!type) return -1;
  return ps_type_format_canonical_signature_for_data_layout(
      type, ps_ctx_data_layout(context), out, out_size);
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

bool psx_ctx_get_type_token_layout_in(
    const psx_semantic_context_t *context, token_kind_t kind,
    int *out_size, int *out_alignment) {
  ag_target_scalar_kind_t scalar_kind = AG_TARGET_SCALAR_COUNT;
  switch (kind) {
    case TK_CHAR:
    case TK_BOOL:
      scalar_kind = AG_TARGET_SCALAR_CHAR;
      break;
    case TK_SHORT:
      scalar_kind = AG_TARGET_SCALAR_SHORT;
      break;
    case TK_INT:
    case TK_SIGNED:
    case TK_UNSIGNED:
      scalar_kind = AG_TARGET_SCALAR_INT;
      break;
    case TK_LONG:
      scalar_kind = AG_TARGET_SCALAR_LONG;
      break;
    case TK_FLOAT:
      scalar_kind = AG_TARGET_SCALAR_FLOAT;
      break;
    case TK_DOUBLE:
      scalar_kind = AG_TARGET_SCALAR_DOUBLE;
      break;
    case TK_COMPLEX:
      scalar_kind = AG_TARGET_SCALAR_DOUBLE_COMPLEX;
      break;
    default:
      return false;
  }
  const ag_data_layout_t *data_layout = ps_ctx_data_layout(context);
  if (out_size)
    *out_size = ag_data_layout_scalar_size(data_layout, scalar_kind);
  if (out_alignment)
    *out_alignment = ag_data_layout_scalar_alignment(data_layout, scalar_kind);
  return true;
}
