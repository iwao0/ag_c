#include "typed_hir_materialization.h"

#include <stdlib.h>
#include <string.h>

#include "../parser/arena.h"
#include "../parser/ast.h"
#include "../parser/gvar_public.h"
#include "../parser/lvar_public.h"
#include "../parser/node_resolution_state.h"
#include "../parser/node_type_public.h"
#include "../parser/node_vla_public.h"
#include "../parser/semantic_ctx.h"
#include "../parser/vla_runtime.h"
#include "../type_layout.h"
#include "typed_hir_node_internal.h"
#include "resolved_node_kind.h"
#include "typed_hir_tree_internal.h"
#include "resolution_work_tree.h"

typedef struct {
  arena_context_t *arena_context;
  const psx_semantic_context_t *semantic_context;
  psx_resolved_hir_build_failure_t *failure;
} hir_materializer_t;

typedef struct {
  psx_resolved_hir_node_t **items;
  psx_hir_edge_kind_t *edges;
  size_t count;
  size_t capacity;
} hir_children_t;

static void set_failure(
    hir_materializer_t *builder, psx_resolved_hir_build_status_t status,
    const node_t *source) {
  if (!builder->failure ||
      builder->failure->status != PSX_RESOLVED_HIR_BUILD_OK)
    return;
  builder->failure->status = status;
  builder->failure->source_node_kind = source ? (int)source->kind : -1;
}

static int append_child(
    hir_materializer_t *builder, hir_children_t *children,
    psx_resolved_hir_node_t *child, psx_hir_edge_kind_t edge,
    const node_t *source) {
  if (!child) return 0;
  if (children->count == children->capacity) {
    size_t capacity = children->capacity ? children->capacity * 2 : 4;
    psx_resolved_hir_node_t **items = realloc(
        children->items, capacity * sizeof(*items));
    if (!items) {
      set_failure(builder, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY, source);
      return 0;
    }
    children->items = items;
    psx_hir_edge_kind_t *edges = realloc(
        children->edges, capacity * sizeof(*edges));
    if (!edges) {
      set_failure(builder, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY, source);
      return 0;
    }
    children->edges = edges;
    children->capacity = capacity;
  }
  children->items[children->count++] = child;
  children->edges[children->count - 1] = edge;
  return 1;
}

static int map_kind(
    psx_work_node_kind_t source, psx_hir_node_kind_t *kind,
    psx_hir_node_role_t *role) {
#define MAP_EXPR(source_kind, hir_kind) \
  case source_kind: *kind = hir_kind; *role = PSX_HIR_ROLE_EXPRESSION; return 1
#define MAP_STMT(source_kind, hir_kind) \
  case source_kind: *kind = hir_kind; *role = PSX_HIR_ROLE_STATEMENT; return 1
  switch (source) {
    MAP_EXPR(ND_ADD, PSX_HIR_ADD);
    MAP_EXPR(ND_SUB, PSX_HIR_SUB);
    MAP_EXPR(ND_MUL, PSX_HIR_MUL);
    MAP_EXPR(ND_DIV, PSX_HIR_DIV);
    MAP_EXPR(ND_MOD, PSX_HIR_MOD);
    MAP_EXPR(ND_EQ, PSX_HIR_EQ);
    MAP_EXPR(ND_NE, PSX_HIR_NE);
    MAP_EXPR(ND_LT, PSX_HIR_LT);
    MAP_EXPR(ND_LE, PSX_HIR_LE);
    MAP_EXPR(ND_BITAND, PSX_HIR_BITAND);
    MAP_EXPR(ND_BITXOR, PSX_HIR_BITXOR);
    MAP_EXPR(ND_BITOR, PSX_HIR_BITOR);
    MAP_EXPR(ND_SHL, PSX_HIR_SHL);
    MAP_EXPR(ND_SHR, PSX_HIR_SHR);
    MAP_EXPR(ND_LOGAND, PSX_HIR_LOGAND);
    MAP_EXPR(ND_LOGOR, PSX_HIR_LOGOR);
    MAP_EXPR(ND_TERNARY, PSX_HIR_TERNARY);
    MAP_EXPR(ND_COMMA, PSX_HIR_COMMA);
    MAP_EXPR(ND_ASSIGN, PSX_HIR_ASSIGN);
    MAP_EXPR(ND_LVAR, PSX_HIR_LOCAL);
    MAP_STMT(ND_IF, PSX_HIR_IF);
    MAP_STMT(ND_WHILE, PSX_HIR_WHILE);
    MAP_STMT(ND_DO_WHILE, PSX_HIR_DO_WHILE);
    MAP_STMT(ND_FOR, PSX_HIR_FOR);
    MAP_STMT(ND_SWITCH, PSX_HIR_SWITCH);
    MAP_STMT(ND_CASE, PSX_HIR_CASE);
    MAP_STMT(ND_DEFAULT, PSX_HIR_DEFAULT);
    MAP_STMT(ND_BREAK, PSX_HIR_BREAK);
    MAP_STMT(ND_CONTINUE, PSX_HIR_CONTINUE);
    MAP_STMT(ND_GOTO, PSX_HIR_GOTO);
    MAP_STMT(ND_LABEL, PSX_HIR_LABEL);
    MAP_EXPR(ND_PRE_INC, PSX_HIR_PRE_INC);
    MAP_EXPR(ND_PRE_DEC, PSX_HIR_PRE_DEC);
    MAP_EXPR(ND_POST_INC, PSX_HIR_POST_INC);
    MAP_EXPR(ND_POST_DEC, PSX_HIR_POST_DEC);
    MAP_STMT(ND_RETURN, PSX_HIR_RETURN);
    MAP_STMT(ND_BLOCK, PSX_HIR_BLOCK);
    MAP_STMT(ND_FUNCDEF, PSX_HIR_FUNCTION);
    MAP_EXPR(ND_FUNCALL, PSX_HIR_CALL);
    MAP_EXPR(ND_FUNCREF, PSX_HIR_FUNCTION_REF);
    MAP_EXPR(ND_UNARY_NEGATE, PSX_HIR_NEGATE);
    MAP_EXPR(ND_UNARY_DEREF, PSX_HIR_DEREF);
    MAP_EXPR(ND_DEREF, PSX_HIR_DEREF);
    MAP_EXPR(ND_SUBSCRIPT, PSX_HIR_SUBSCRIPT);
    MAP_EXPR(ND_MEMBER_ACCESS, PSX_HIR_MEMBER_ACCESS);
    MAP_EXPR(ND_ALIGNOF_QUERY, PSX_HIR_NUMBER);
    MAP_EXPR(ND_ADDR, PSX_HIR_ADDRESS);
    MAP_EXPR(ND_STRING, PSX_HIR_STRING);
    MAP_EXPR(ND_NUM, PSX_HIR_NUMBER);
    MAP_EXPR(ND_GVAR, PSX_HIR_GLOBAL);
    MAP_STMT(ND_VLA_ALLOC, PSX_HIR_VLA_ALLOC);
    MAP_EXPR(ND_FP_TO_INT, PSX_HIR_FP_TO_INT);
    MAP_EXPR(ND_INT_TO_FP, PSX_HIR_INT_TO_FP);
    MAP_EXPR(ND_VA_ARG_AREA, PSX_HIR_VA_ARG_AREA);
    MAP_EXPR(ND_CAST, PSX_HIR_CAST);
    MAP_EXPR(ND_CREAL, PSX_HIR_CREAL);
    MAP_EXPR(ND_CIMAG, PSX_HIR_CIMAG);
    MAP_EXPR(ND_STMT_EXPR, PSX_HIR_STMT_EXPR);
    default:
      return 0;
  }
#undef MAP_EXPR
#undef MAP_STMT
}

static int compound_operator(
    token_kind_t source, psx_hir_compound_operator_t *result) {
  if (!result) return 0;
  switch (source) {
    case TK_PLUSEQ: *result = PSX_HIR_COMPOUND_ADD; return 1;
    case TK_MINUSEQ: *result = PSX_HIR_COMPOUND_SUB; return 1;
    case TK_MULEQ: *result = PSX_HIR_COMPOUND_MUL; return 1;
    case TK_DIVEQ: *result = PSX_HIR_COMPOUND_DIV; return 1;
    case TK_MODEQ: *result = PSX_HIR_COMPOUND_MOD; return 1;
    case TK_SHLEQ: *result = PSX_HIR_COMPOUND_SHL; return 1;
    case TK_SHREQ: *result = PSX_HIR_COMPOUND_SHR; return 1;
    case TK_ANDEQ: *result = PSX_HIR_COMPOUND_BITAND; return 1;
    case TK_XOREQ: *result = PSX_HIR_COMPOUND_BITXOR; return 1;
    case TK_OREQ: *result = PSX_HIR_COMPOUND_BITOR; return 1;
    default: return 0;
  }
}

static psx_resolved_hir_node_t *build_node(
    hir_materializer_t *builder, const node_t *source);
static psx_resolved_hir_node_t *materialize_node_record(
    hir_materializer_t *builder, const psx_hir_node_spec_t *spec,
    psx_hir_node_role_t role, psx_qual_type_t qual_type,
    const hir_children_t *children,
    const psx_hir_symbol_spec_t *symbol,
    const node_t *source);
static int canonical_type_exists(
    const hir_materializer_t *builder, psx_qual_type_t type);

static psx_resolved_hir_node_t *materialize_expression(
    hir_materializer_t *builder, psx_hir_node_kind_t kind,
    psx_qual_type_t qual_type, const hir_children_t *children,
    const node_t *source) {
  psx_hir_node_spec_t spec = {
      .kind = kind,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
  };
  return materialize_node_record(
      builder, &spec, PSX_HIR_ROLE_EXPRESSION, qual_type,
      children, NULL, source);
}

static psx_resolved_hir_node_t *materialize_comma(
    hir_materializer_t *builder,
    psx_resolved_hir_node_t *lhs, psx_resolved_hir_node_t *rhs,
    psx_qual_type_t qual_type, const node_t *source) {
  psx_resolved_hir_node_t *items[] = {lhs, rhs};
  psx_hir_edge_kind_t edges[] = {
      PSX_HIR_EDGE_LHS, PSX_HIR_EDGE_RHS};
  hir_children_t children = {
      .items = items,
      .edges = edges,
      .count = 2,
      .capacity = 2,
  };
  return materialize_expression(
      builder, PSX_HIR_COMMA, qual_type, &children, source);
}

static int materialize_sizeof_vla_indices(
    hir_materializer_t *builder, const node_t *operand,
    psx_resolved_hir_node_t **prefix, const node_t *source) {
  if (!operand || operand->kind != ND_SUBSCRIPT) return 1;
  if (!materialize_sizeof_vla_indices(
          builder, operand->lhs, prefix, source))
    return 0;
  psx_resolved_hir_node_t *index =
      build_node(builder, operand->rhs);
  if (!index) return 0;
  if (!*prefix) {
    *prefix = index;
    return 1;
  }
  *prefix = materialize_comma(
      builder, *prefix, index, index->expression_type, source);
  return *prefix != NULL;
}

static psx_resolved_hir_node_t *materialize_sizeof_value(
    hir_materializer_t *builder, const node_sizeof_query_t *query,
    psx_qual_type_t qual_type) {
  if (query->runtime_size_expr)
    return build_node(builder, query->runtime_size_expr);
  if (query->runtime_size_slot != 0) {
    psx_hir_node_spec_t spec = {
        .kind = PSX_HIR_LOCAL,
        .attached_qual_type = {
            PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
        .storage_offset = query->runtime_size_slot,
        .object_offset = query->runtime_size_slot,
        .object_size = PSX_VLA_RUNTIME_SLOT_SIZE,
        .object_align = PSX_VLA_RUNTIME_SLOT_SIZE,
    };
    return materialize_node_record(
        builder, &spec, PSX_HIR_ROLE_EXPRESSION, qual_type,
        NULL, NULL, &query->base);
  }
  psx_hir_node_spec_t spec = {
      .kind = PSX_HIR_NUMBER,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
      .integer_value = query->resolved_size,
  };
  return materialize_node_record(
      builder, &spec, PSX_HIR_ROLE_EXPRESSION, qual_type,
      NULL, NULL, &query->base);
}

static psx_resolved_hir_node_t *materialize_sizeof_query(
    hir_materializer_t *builder, const node_sizeof_query_t *query) {
  const node_t *source = &query->base;
  psx_qual_type_t qual_type = ps_node_qual_type(source);
  if (!canonical_type_exists(builder, qual_type)) {
    set_failure(
        builder, PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE, source);
    return NULL;
  }
  psx_resolved_hir_node_t *value =
      materialize_sizeof_value(builder, query, qual_type);
  if (!value) return NULL;
  if (!query->evaluates_vla_operand) return value;
  psx_resolved_hir_node_t *prefix = NULL;
  if (!materialize_sizeof_vla_indices(
          builder, query->operand, &prefix, source))
    return NULL;
  if (!prefix) {
    set_failure(
        builder, PSX_RESOLVED_HIR_BUILD_RAW_SYNTAX_REMAINS, source);
    return NULL;
  }
  return materialize_comma(
      builder, prefix, value, qual_type, source);
}

static int is_statement_expression_value(
    const node_t *statement_expression, const node_t *candidate) {
  const node_t *value = statement_expression
                            ? statement_expression->rhs : NULL;
  if (!value || !candidate) return 0;
  if (candidate == value) return 1;
  return candidate->kind == value->kind && candidate->tok &&
         candidate->tok == value->tok;
}

static psx_resolved_hir_node_t *materialize_node_record(
    hir_materializer_t *builder, const psx_hir_node_spec_t *spec,
    psx_hir_node_role_t role, psx_qual_type_t qual_type,
    const hir_children_t *children,
    const psx_hir_symbol_spec_t *symbol,
    const node_t *source) {
  psx_resolved_hir_node_t *node = arena_alloc_in(
      builder->arena_context, sizeof(*node));
  if (!node) {
    set_failure(builder, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY, source);
    return NULL;
  }
  node->spec = *spec;
  node->role = role;
  node->expression_type = qual_type;
  node->source_node_kind = source ? (int)source->kind : -1;
  node->spec.children = NULL;
  node->spec.child_edges = NULL;
  node->spec.child_count = children ? children->count : 0;
  if (children && children->count) {
    node->children = arena_alloc_in(
        builder->arena_context,
        children->count * sizeof(*node->children));
    node->child_edges = arena_alloc_in(
        builder->arena_context,
        children->count * sizeof(*node->child_edges));
    if (!node->children || !node->child_edges) {
      set_failure(builder, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY, source);
      return NULL;
    }
    memcpy(node->children, children->items,
           children->count * sizeof(*node->children));
    memcpy(node->child_edges, children->edges,
           children->count * sizeof(*node->child_edges));
  }
  if (symbol) {
    node->symbol = *symbol;
    node->has_symbol = 1;
  }
  return node;
}

static psx_resolved_hir_node_t *build_statement_expression_prefix(
    hir_materializer_t *builder, const node_t *source) {
  const node_block_t *block =
      source && source->lhs && source->lhs->kind == ND_BLOCK
          ? (const node_block_t *)source->lhs : NULL;
  if (!block) {
    set_failure(builder, PSX_RESOLVED_HIR_BUILD_RAW_SYNTAX_REMAINS, source);
    return NULL;
  }
  hir_children_t children = {0};
  for (int i = 0; block->body && block->body[i]; i++) {
    if (is_statement_expression_value(source, block->body[i])) continue;
    if (!append_child(
            builder, &children, build_node(builder, block->body[i]),
            PSX_HIR_EDGE_BLOCK_ITEM, block->body[i])) {
      free(children.items);
      free(children.edges);
      return NULL;
    }
  }
  psx_hir_node_spec_t spec = {
      .kind = PSX_HIR_BLOCK,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
  };
  psx_resolved_hir_node_t *result = materialize_node_record(
      builder, &spec, PSX_HIR_ROLE_STATEMENT,
      (psx_qual_type_t){
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
      &children, NULL, source);
  free(children.items);
  free(children.edges);
  return result;
}

static int build_and_append(
    hir_materializer_t *builder, hir_children_t *children,
    const node_t *source, psx_hir_edge_kind_t edge) {
  if (!source) return 1;
  return append_child(
      builder, children, build_node(builder, source), edge, source);
}

static int build_special_children(
    hir_materializer_t *builder, hir_children_t *children,
    const node_t *source, int *include_common_children) {
  *include_common_children = 1;
  switch (source->kind) {
    case ND_BLOCK: {
      const node_block_t *block = (const node_block_t *)source;
      *include_common_children = 0;
      for (int i = 0; block->body && block->body[i]; i++) {
        if (!build_and_append(
                builder, children, block->body[i],
                PSX_HIR_EDGE_BLOCK_ITEM)) return 0;
      }
      return 1;
    }
    case ND_FUNCDEF: {
      const node_function_definition_t *function =
          (const node_function_definition_t *)source;
      for (int i = 0; i < function->parameter_count; i++) {
        if (!build_and_append(
                builder, children, function->parameters[i],
                PSX_HIR_EDGE_PARAMETER))
          return 0;
      }
      return 1;
    }
    case ND_FUNCALL: {
      const node_function_call_t *call =
          (const node_function_call_t *)source;
      if (!build_and_append(
              builder, children, call->callee,
              PSX_HIR_EDGE_CALLEE)) return 0;
      for (int i = 0; i < call->argument_count; i++) {
        if (!build_and_append(
                builder, children, call->arguments[i],
                PSX_HIR_EDGE_ARGUMENT))
          return 0;
      }
      return 1;
    }
    case ND_STMT_EXPR:
      *include_common_children = 0;
      return append_child(
                 builder, children,
                 build_statement_expression_prefix(builder, source),
                 PSX_HIR_EDGE_LHS, source) &&
             build_and_append(
                 builder, children, source->rhs, PSX_HIR_EDGE_RHS);
    case ND_IF:
    case ND_FOR:
    case ND_TERNARY: {
      const node_ctrl_t *control = (const node_ctrl_t *)source;
      return build_and_append(
                 builder, children, control->init, PSX_HIR_EDGE_INIT) &&
             build_and_append(
                 builder, children, control->inc,
                 PSX_HIR_EDGE_INCREMENT) &&
             build_and_append(
                 builder, children, control->els, PSX_HIR_EDGE_ELSE);
    }
    default:
      return 1;
  }
}

static int canonical_type_exists(
    const hir_materializer_t *builder, psx_qual_type_t type) {
  return type.type_id != PSX_TYPE_ID_INVALID &&
         ps_ctx_type_by_id_in(builder->semantic_context, type.type_id) != NULL;
}

static psx_qual_type_t child_qual_type(
    const hir_materializer_t *builder, const hir_children_t *children,
    psx_hir_edge_kind_t edge) {
  if (!builder || !children) {
    return (psx_qual_type_t){
        PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  }
  for (size_t i = 0; i < children->count; i++) {
    if (children->edges[i] != edge) continue;
    const psx_resolved_hir_node_t *child = children->items[i];
    if (child) return child->expression_type;
  }
  return (psx_qual_type_t){
      PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
}

static int derive_structural_expression_type(
    hir_materializer_t *builder, const node_t *source,
    const hir_children_t *children, psx_qual_type_t *qual_type) {
  psx_qual_type_t derived = {
      PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  switch (source->kind) {
    case ND_COMMA:
      derived = child_qual_type(
          builder, children, PSX_HIR_EDGE_RHS);
      break;
    case ND_ASSIGN:
      derived = child_qual_type(
          builder, children, PSX_HIR_EDGE_LHS);
      break;
    default:
      return 1;
  }
  if (!canonical_type_exists(builder, derived)) {
    set_failure(
        builder, PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE, source);
    return 0;
  }
  *qual_type = derived;
  return 1;
}

static int resolved_global_symbol_spec(
    hir_materializer_t *builder, const global_var_t *global,
    const node_t *source, psx_hir_symbol_spec_t *symbol) {
  if (!global) {
    set_failure(
        builder, PSX_RESOLVED_HIR_BUILD_MISSING_RESOLVED_SYMBOL, source);
    return 0;
  }
  psx_qual_type_t qual_type = ps_gvar_decl_qual_type(global);
  if (!canonical_type_exists(builder, qual_type)) {
    set_failure(
        builder, PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE, source);
    return 0;
  }
  const psx_semantic_type_table_t *semantic_types =
      ps_ctx_semantic_type_table_in(builder->semantic_context);
  const psx_record_layout_table_t *record_layouts =
      ps_ctx_record_layout_table_in(builder->semantic_context);
  const ag_target_info_t *target =
      ps_ctx_target_info(builder->semantic_context);
  int byte_size = ps_type_sizeof_id_with_records(
      semantic_types, record_layouts, qual_type.type_id, target);
  int alignment = ps_type_alignof_id_with_records(
      semantic_types, record_layouts, qual_type.type_id, target);
  if ((byte_size <= 0 || alignment <= 0) &&
      ps_gvar_is_extern_decl(global)) {
    psx_qual_type_t base = psx_semantic_type_table_base(
        semantic_types, qual_type.type_id);
    if (base.type_id != PSX_TYPE_ID_INVALID) {
      if (byte_size <= 0)
        byte_size = ps_type_sizeof_id_with_records(
            semantic_types, record_layouts, base.type_id, target);
      if (alignment <= 0)
        alignment = ps_type_alignof_id_with_records(
            semantic_types, record_layouts, base.type_id, target);
    }
  }
  *symbol = (psx_hir_symbol_spec_t){
      .name = ps_gvar_name(global),
      .name_length = ps_gvar_name_len(global) > 0
                         ? (size_t)ps_gvar_name_len(global) : 0,
      .qual_type = qual_type,
      .byte_size = byte_size,
      .alignment = alignment,
      .is_extern = ps_gvar_is_extern_decl(global) ? 1 : 0,
      .is_static = ps_gvar_is_static_storage(global) ? 1 : 0,
      .is_thread_local = ps_gvar_is_thread_local(global) ? 1 : 0,
  };
  return 1;
}

static int attach_global_symbol(
    hir_materializer_t *builder, const node_t *source,
    psx_hir_symbol_spec_t *symbol, int *has_symbol) {
  if (has_symbol) *has_symbol = 0;
  if (source->kind != ND_GVAR) return 1;
  const global_var_t *global = ((const node_gvar_t *)source)->symbol;
  if (!resolved_global_symbol_spec(
          builder, global, source, symbol))
    return 0;
  if (has_symbol) *has_symbol = 1;
  return 1;
}

static psx_resolved_hir_node_t *materialize_local_object_reference(
    hir_materializer_t *builder, const lvar_t *local,
    const node_t *source) {
  if (!local) {
    set_failure(
        builder, PSX_RESOLVED_HIR_BUILD_MISSING_RESOLVED_SYMBOL, source);
    return NULL;
  }
  psx_qual_type_t qual_type = ps_lvar_decl_qual_type(local);
  if (!canonical_type_exists(builder, qual_type)) {
    set_failure(
        builder, PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE, source);
    return NULL;
  }
  psx_hir_node_spec_t spec = {
      .kind = PSX_HIR_LOCAL,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
      .storage_offset = ps_lvar_offset(local),
      .object_offset = ps_lvar_offset(local),
      .object_size = ps_lvar_frame_storage_size(local),
      .object_align = ps_lvar_align_bytes(local),
  };
  return materialize_node_record(
      builder, &spec, PSX_HIR_ROLE_EXPRESSION, qual_type,
      NULL, NULL, source);
}

static psx_resolved_hir_node_t *materialize_global_object_reference(
    hir_materializer_t *builder, const global_var_t *global,
    const node_t *source) {
  psx_hir_symbol_spec_t symbol = {0};
  if (!resolved_global_symbol_spec(
          builder, global, source, &symbol))
    return NULL;
  psx_hir_node_spec_t spec = {
      .kind = PSX_HIR_GLOBAL,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
      .name = ps_gvar_name(global),
      .name_length = ps_gvar_name_len(global) > 0
                         ? (size_t)ps_gvar_name_len(global) : 0,
  };
  return materialize_node_record(
      builder, &spec, PSX_HIR_ROLE_EXPRESSION,
      ps_gvar_decl_qual_type(global), NULL, &symbol, source);
}

static psx_resolved_hir_node_t *materialize_address_of_object(
    hir_materializer_t *builder,
    psx_resolved_hir_node_t *object,
    psx_qual_type_t result_type, const node_t *source) {
  psx_resolved_hir_node_t *items[] = {object};
  psx_hir_edge_kind_t edges[] = {PSX_HIR_EDGE_LHS};
  hir_children_t children = {
      .items = items,
      .edges = edges,
      .count = 1,
      .capacity = 1,
  };
  return materialize_expression(
      builder, PSX_HIR_ADDRESS, result_type, &children, source);
}

static psx_resolved_hir_node_t *materialize_compound_literal(
    hir_materializer_t *builder,
    const node_compound_literal_t *compound) {
  const node_t *source = &compound->base;
  const psx_compound_literal_resolution_t *resolution =
      source->resolution_state
          ? &source->resolution_state->compound_literal : NULL;
  psx_qual_type_t result_type = ps_node_qual_type(source);
  if (!resolution || !resolution->is_planned) {
    set_failure(
        builder, PSX_RESOLVED_HIR_BUILD_RAW_SYNTAX_REMAINS, source);
    return NULL;
  }
  if (!canonical_type_exists(builder, result_type)) {
    set_failure(
        builder, PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE, source);
    return NULL;
  }
  if (resolution->direct_value)
    return build_node(builder, resolution->direct_value);

  psx_resolved_hir_node_t *object =
      resolution->local_object
          ? materialize_local_object_reference(
                builder, resolution->local_object, source)
          : materialize_global_object_reference(
                builder, resolution->global_object, source);
  if (!object) return NULL;
  psx_resolved_hir_node_t *value =
      compound->requires_addressable_object
          ? materialize_address_of_object(
                builder, object, result_type, source)
          : object;
  if (!value) return NULL;
  if (!resolution->runtime_initialization) return value;
  psx_resolved_hir_node_t *initialization =
      build_node(builder, resolution->runtime_initialization);
  if (!initialization) return NULL;
  return materialize_comma(
      builder, initialization, value, result_type, source);
}

static int copy_payload(
    hir_materializer_t *builder, const node_t *source,
    psx_hir_node_spec_t *spec) {
  switch (source->kind) {
    case ND_ASSIGN:
      if (source->is_source_compound_assignment) {
        psx_hir_compound_operator_t op = PSX_HIR_COMPOUND_ADD;
        if (compound_operator(source->source_op, &op))
          spec->integer_value = op;
      }
      break;
    case ND_ALIGNOF_QUERY:
      spec->integer_value =
          ((const node_alignof_query_t *)source)->resolved_alignment;
      break;
    case ND_NUM: {
      const node_num_t *number = (const node_num_t *)source;
      spec->integer_value = number->val;
      spec->floating_value = number->fval;
      break;
    }
    case ND_LVAR: {
      const node_lvar_t *local = (const node_lvar_t *)source;
      spec->storage_offset = local->offset;
      if (local->var) {
        spec->object_offset = ps_lvar_offset(local->var);
        spec->object_size = ps_lvar_frame_storage_size(local->var);
        spec->object_align = ps_lvar_align_bytes(local->var);
      } else {
        spec->object_offset = local->offset;
      }
      break;
    }
    case ND_MEMBER_ACCESS: {
      const node_member_access_t *access =
          (const node_member_access_t *)source;
      const psx_record_layout_t *layout =
          psx_record_layout_table_lookup(
              ps_ctx_record_layout_table_in(builder->semantic_context),
              access->resolved_record_id,
              ps_ctx_target_info(builder->semantic_context));
      const psx_record_member_layout_t *member =
          psx_record_layout_member(layout, access->resolved_member_index);
      if (!access->resolved_member || !member) {
        set_failure(
            builder, PSX_RESOLVED_HIR_BUILD_RAW_SYNTAX_REMAINS, source);
        return 0;
      }
      spec->member_offset = member->offset;
      spec->member_from_pointer = access->from_pointer ? 1 : 0;
      if (access->resolved_member->bit_width > 0) {
        spec->bit_width =
            (unsigned char)access->resolved_member->bit_width;
        spec->bit_offset = (unsigned char)member->bit_offset;
        spec->bit_is_signed =
            access->resolved_member->bit_is_signed ? 1 : 0;
      }
      break;
    }
    case ND_STRING: {
      const node_string_t *string = (const node_string_t *)source;
      spec->name = string->string_label;
      spec->name_length = string->string_label
                              ? strlen(string->string_label) : 0;
      spec->literal_contents = string->literal_contents;
      spec->literal_length = string->literal_length > 0
                                 ? (size_t)string->literal_length : 0;
      int character_width = (int)string->char_width;
      if (character_width <= 0) character_width = 1;
      spec->object_size = (string->byte_len + 1) * character_width;
      spec->object_align = character_width;
      break;
    }
    case ND_FUNCDEF: {
      const node_function_definition_t *function =
          (const node_function_definition_t *)source;
      spec->name = function->name;
      spec->name_length = function->name_len > 0
                              ? (size_t)function->name_len : 0;
      spec->attached_qual_type =
          ps_function_definition_signature_qual_type(function);
      spec->is_static_function = function->is_static ? 1 : 0;
      break;
    }
    case ND_FUNCALL: {
      const node_function_call_t *call =
          (const node_function_call_t *)source;
      spec->name = call->direct_name;
      spec->name_length = call->direct_name_len > 0
                              ? (size_t)call->direct_name_len : 0;
      spec->attached_qual_type = ps_function_call_callee_qual_type(call);
      break;
    }
    case ND_FUNCREF: {
      const node_funcref_t *function = (const node_funcref_t *)source;
      spec->name = function->funcname;
      spec->name_length = function->funcname_len > 0
                              ? (size_t)function->funcname_len : 0;
      break;
    }
    case ND_GVAR: {
      const node_gvar_t *global = (const node_gvar_t *)source;
      spec->name = global->name;
      spec->name_length = global->name_len > 0
                              ? (size_t)global->name_len : 0;
      break;
    }
    case ND_CASE: {
      const node_case_t *case_node = (const node_case_t *)source;
      spec->integer_value = case_node->val;
      spec->label_id = case_node->label_id;
      break;
    }
    case ND_DEFAULT:
      spec->label_id = ((const node_default_t *)source)->label_id;
      break;
    case ND_GOTO:
    case ND_LABEL: {
      const node_jump_t *jump = (const node_jump_t *)source;
      spec->name = jump->name;
      spec->name_length = jump->name_len > 0
                              ? (size_t)jump->name_len : 0;
      spec->label_id = jump->label_id;
      break;
    }
    case ND_VLA_ALLOC: {
      const node_vla_alloc_t *allocation =
          (const node_vla_alloc_t *)source;
      spec->storage_offset =
          allocation->descriptor_frame_off;
      spec->vla_stride_frame_offset =
          allocation->row_stride_frame_off;
      spec->vla_stride_slot_size = PSX_VLA_RUNTIME_SLOT_SIZE;
      break;
    }
    default:
      break;
  }
  return 1;
}

static int copy_vla_payload(
    hir_materializer_t *builder, const node_t *source,
    psx_hir_node_spec_t *spec) {
  if (source->kind == ND_SUBSCRIPT) {
    spec->vla_stride_frame_offset =
        ps_node_vla_row_stride_frame_off((node_t *)source);
    if (spec->vla_stride_frame_offset != 0)
      spec->vla_stride_slot_size = PSX_VLA_RUNTIME_SLOT_SIZE;
    return 1;
  }
  if (source->kind != ND_LVAR) return 1;
  const lvar_t *var = ((const node_lvar_t *)source)->var;
  if (!var || !ps_lvar_is_vla(var)) return 1;
  spec->vla_stride_frame_offset =
      ps_lvar_vla_row_stride_frame_off(var);
  spec->vla_stride_source_offset =
      ps_lvar_vla_row_stride_src_offset(var);
  spec->vla_stride_element_size =
      ps_lvar_vla_row_stride_elem_size(var);
  spec->vla_stride_slot_size = PSX_VLA_RUNTIME_SLOT_SIZE;
  int count = ps_lvar_vla_param_inner_dim_count(var);
  if (count <= 0) return 1;
  int *constants = arena_alloc_in(
      builder->arena_context, (size_t)count * sizeof(*constants));
  int *source_offsets = arena_alloc_in(
      builder->arena_context, (size_t)count * sizeof(*source_offsets));
  if (!constants || !source_offsets) {
    set_failure(builder, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY, source);
    return 0;
  }
  for (int i = 0; i < count; i++) {
    constants[i] = ps_lvar_vla_param_inner_dim_const(var, i);
    source_offsets[i] =
        ps_lvar_vla_param_inner_dim_src_offset(var, i);
  }
  spec->vla_dimension_constants = constants;
  spec->vla_dimension_source_offsets = source_offsets;
  spec->vla_dimension_count = (size_t)count;
  return 1;
}

static psx_resolved_hir_node_t *build_node(
    hir_materializer_t *builder, const node_t *source) {
  if (source && source->kind == ND_COMPOUND_LITERAL) {
    return materialize_compound_literal(
        builder, (const node_compound_literal_t *)source);
  }
  if (source && source->kind == ND_SIZEOF_QUERY) {
    return materialize_sizeof_query(
        builder, (const node_sizeof_query_t *)source);
  }
  if (source && source->kind == ND_GENERIC_SELECTION) {
    const node_generic_selection_t *selection =
        (const node_generic_selection_t *)source;
    psx_qual_type_t qual_type = ps_node_qual_type(source);
    int selected = selection->selected_index;
    if (!canonical_type_exists(builder, qual_type)) {
      set_failure(
          builder, PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE, source);
      return NULL;
    }
    if (selected < 0 || selected >= selection->association_count ||
        !selection->associations[selected].expression) {
      set_failure(
          builder, PSX_RESOLVED_HIR_BUILD_RAW_SYNTAX_REMAINS, source);
      return NULL;
    }
    return build_node(
        builder, selection->associations[selected].expression);
  }
  psx_hir_node_spec_t spec = {0};
  psx_hir_node_role_t role = PSX_HIR_ROLE_STATEMENT;
  psx_qual_type_t qual_type = {
      PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  spec.attached_qual_type = qual_type;
  if (!map_kind(source->kind, &spec.kind, &role)) {
    set_failure(builder, PSX_RESOLVED_HIR_BUILD_RAW_SYNTAX_REMAINS, source);
    return NULL;
  }
  if (source->kind == ND_ASSIGN &&
      source->is_source_compound_assignment)
    spec.kind = PSX_HIR_COMPOUND_ASSIGN;
  if (role == PSX_HIR_ROLE_EXPRESSION) {
    qual_type = ps_node_qual_type(source);
    if (!canonical_type_exists(builder, qual_type)) {
      set_failure(
          builder, PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE, source);
      return NULL;
    }
  }
  if (!copy_payload(builder, source, &spec))
    return NULL;
  {
    int bit_width = 0;
    int bit_offset = 0;
    int bit_is_signed = 0;
    if (source->kind != ND_MEMBER_ACCESS &&
        ps_node_bitfield_info(
            (node_t *)source, &bit_width, &bit_offset,
            &bit_is_signed)) {
      spec.bit_width = (unsigned char)bit_width;
      spec.bit_offset = (unsigned char)bit_offset;
      spec.bit_is_signed = bit_is_signed ? 1 : 0;
    }
  }
  psx_hir_symbol_spec_t symbol = {0};
  int has_symbol = 0;
  if (!attach_global_symbol(
          builder, source, &symbol, &has_symbol))
    return NULL;
  if (source->kind == ND_FUNCDEF &&
      !canonical_type_exists(builder, spec.attached_qual_type)) {
    set_failure(
        builder, PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE, source);
    return NULL;
  }
  if (spec.attached_qual_type.type_id != PSX_TYPE_ID_INVALID &&
      !canonical_type_exists(builder, spec.attached_qual_type)) {
    set_failure(
        builder, PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE, source);
    return NULL;
  }
  if (!copy_vla_payload(builder, source, &spec))
    return NULL;

  hir_children_t children = {0};
  int include_common_children = 1;
  if (!build_special_children(
          builder, &children, source, &include_common_children) ||
      (include_common_children &&
       (!build_and_append(
            builder, &children, source->lhs, PSX_HIR_EDGE_LHS) ||
        !build_and_append(
            builder, &children, source->rhs,
            source->kind == ND_FUNCDEF
                ? PSX_HIR_EDGE_FUNCTION_BODY : PSX_HIR_EDGE_RHS)))) {
    free(children.items);
    free(children.edges);
    return NULL;
  }
  if (role == PSX_HIR_ROLE_EXPRESSION &&
      !derive_structural_expression_type(
          builder, source, &children, &qual_type)) {
    free(children.items);
    free(children.edges);
    return NULL;
  }
  psx_resolved_hir_node_t *result = materialize_node_record(
      builder, &spec, role, qual_type, &children,
      has_symbol ? &symbol : NULL, source);
  free(children.items);
  free(children.edges);
  return result;
}

psx_typed_hir_tree_t *psx_resolution_work_tree_materialize_hir(
    const psx_resolution_work_tree_t *work_tree,
    const psx_semantic_context_t *semantic_context,
    psx_resolved_hir_build_failure_t *failure) {
  if (failure) memset(failure, 0, sizeof(*failure));
  const node_t *semantic_root =
      psx_resolution_work_tree_root(work_tree);
  if (!work_tree || !semantic_context || !semantic_root) {
    if (failure) {
      failure->status = PSX_RESOLVED_HIR_BUILD_INVALID_INPUT;
      failure->source_node_kind = semantic_root
                                      ? (int)semantic_root->kind : -1;
    }
    return NULL;
  }
  psx_resolution_work_phase_t phase =
      psx_resolution_work_tree_phase(work_tree);
  if (phase != PSX_RESOLUTION_WORK_FINALIZED) {
    if (failure) {
      failure->status = PSX_RESOLVED_HIR_BUILD_UNFINALIZED_RESOLUTION;
      failure->source_node_kind = (int)semantic_root->kind;
    }
    return NULL;
  }
  hir_materializer_t builder = {
      .arena_context = ps_ctx_arena(semantic_context),
      .semantic_context = semantic_context,
      .failure = failure,
  };
  psx_resolved_hir_node_t *root = build_node(&builder, semantic_root);
  if (!root) {
    if (failure && failure->status == PSX_RESOLVED_HIR_BUILD_OK)
      failure->status = PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY;
    return NULL;
  }
  psx_typed_hir_tree_t *typed_tree = arena_alloc_in(
      builder.arena_context, sizeof(*typed_tree));
  if (!typed_tree) {
    if (failure && failure->status == PSX_RESOLVED_HIR_BUILD_OK)
      failure->status = PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY;
    return NULL;
  }
  typed_tree->root = root;
  return typed_tree;
}
