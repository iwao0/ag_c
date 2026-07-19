#include "typed_hir_tree_materialization.h"

#include <stdlib.h>
#include <string.h>

#include "../parser/arena.h"
#include "../parser/ast.h"
#include "../parser/gvar_public.h"
#include "../parser/lvar_public.h"
#include "resolution_state.h"
#include "resolved_node_type.h"
#include "../parser/node_vla_public.h"
#include "../parser/semantic_ctx.h"
#include "../parser/vla_runtime.h"
#include "../type_layout.h"
#include "../lowering/runtime_initializer_plan.h"
#include "compound_literal_resolution.h"
#include "alignof_query_resolution.h"
#include "case_label_resolution.h"
#include "function_call_resolution.h"
#include "semantic_node_internal.h"
#include "generic_selection_resolution.h"
#include "hir_local_resolution.h"
#include "hir_symbol_resolution.h"
#include "literal_resolution.h"
#include "member_access_resolution.h"
#include "resolved_node_kind.h"
#include "resolved_node.h"
#include "resolved_object_ref.h"
#include "resolved_function.h"
#include "sizeof_query_resolution.h"
#include "source_cast_resolution.h"
#include "semantic_node_builder.h"
#include "typed_hir_tree_internal.h"
#include "vla_runtime_plan.h"

typedef psx_semantic_node_builder_t hir_materializer_t;

static psx_resolution_store_t *materializer_resolution_store(
    const hir_materializer_t *builder) {
  return builder
             ? ps_ctx_resolution_store(builder->semantic_context)
             : NULL;
}

typedef struct {
  psx_semantic_node_t **items;
  psx_hir_edge_kind_t *edges;
  size_t count;
  size_t capacity;
} hir_children_t;

static void set_failure(
    hir_materializer_t *builder, psx_resolved_hir_build_status_t status,
    const node_t *source) {
  psx_semantic_node_builder_fail(
      builder, status,
      psx_resolution_node_kind(
          materializer_resolution_store(builder), source));
}

static int append_child(
    hir_materializer_t *builder, hir_children_t *children,
    psx_semantic_node_t *child, psx_hir_edge_kind_t edge,
    const node_t *source) {
  if (!child) return 0;
  if (children->count == children->capacity) {
    size_t capacity = children->capacity ? children->capacity * 2 : 4;
    psx_semantic_node_t **items = realloc(
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
    psx_resolution_node_kind_t source, psx_hir_node_kind_t *kind) {
#define MAP(source_kind, hir_kind) \
  case source_kind: *kind = hir_kind; return 1
  switch (source) {
    MAP(ND_ADD, PSX_HIR_ADD);
    MAP(ND_SUB, PSX_HIR_SUB);
    MAP(ND_MUL, PSX_HIR_MUL);
    MAP(ND_DIV, PSX_HIR_DIV);
    MAP(ND_MOD, PSX_HIR_MOD);
    MAP(ND_EQ, PSX_HIR_EQ);
    MAP(ND_NE, PSX_HIR_NE);
    MAP(ND_LT, PSX_HIR_LT);
    MAP(ND_LE, PSX_HIR_LE);
    MAP(ND_BITAND, PSX_HIR_BITAND);
    MAP(ND_BITXOR, PSX_HIR_BITXOR);
    MAP(ND_BITOR, PSX_HIR_BITOR);
    MAP(ND_SHL, PSX_HIR_SHL);
    MAP(ND_SHR, PSX_HIR_SHR);
    MAP(ND_LOGAND, PSX_HIR_LOGAND);
    MAP(ND_LOGOR, PSX_HIR_LOGOR);
    MAP(ND_TERNARY, PSX_HIR_TERNARY);
    MAP(ND_COMMA, PSX_HIR_COMMA);
    MAP(ND_ASSIGN, PSX_HIR_ASSIGN);
    MAP(ND_IF, PSX_HIR_IF);
    MAP(ND_WHILE, PSX_HIR_WHILE);
    MAP(ND_DO_WHILE, PSX_HIR_DO_WHILE);
    MAP(ND_FOR, PSX_HIR_FOR);
    MAP(ND_SWITCH, PSX_HIR_SWITCH);
    MAP(ND_CASE, PSX_HIR_CASE);
    MAP(ND_DEFAULT, PSX_HIR_DEFAULT);
    MAP(ND_BREAK, PSX_HIR_BREAK);
    MAP(ND_CONTINUE, PSX_HIR_CONTINUE);
    MAP(ND_GOTO, PSX_HIR_GOTO);
    MAP(ND_LABEL, PSX_HIR_LABEL);
    MAP(ND_PRE_INC, PSX_HIR_PRE_INC);
    MAP(ND_PRE_DEC, PSX_HIR_PRE_DEC);
    MAP(ND_POST_INC, PSX_HIR_POST_INC);
    MAP(ND_POST_DEC, PSX_HIR_POST_DEC);
    MAP(ND_RETURN, PSX_HIR_RETURN);
    MAP(ND_STATIC_ASSERT, PSX_HIR_NOP);
    MAP(ND_BLOCK, PSX_HIR_BLOCK);
    MAP(ND_FUNCDEF, PSX_HIR_FUNCTION);
    MAP(ND_FUNCALL, PSX_HIR_CALL);
    MAP(ND_UNARY_NEGATE, PSX_HIR_NEGATE);
    MAP(ND_LOGICAL_NOT, PSX_HIR_LOGICAL_NOT);
    MAP(ND_BITWISE_NOT, PSX_HIR_BITWISE_NOT);
    MAP(ND_UNARY_DEREF, PSX_HIR_DEREF);
    MAP(ND_DEREF, PSX_HIR_DEREF);
    MAP(ND_SUBSCRIPT, PSX_HIR_SUBSCRIPT);
    MAP(ND_MEMBER_ACCESS, PSX_HIR_MEMBER_ACCESS);
    MAP(ND_ALIGNOF_QUERY, PSX_HIR_NUMBER);
    MAP(ND_ADDR, PSX_HIR_ADDRESS);
    MAP(ND_VLA_ALLOC, PSX_HIR_VLA_ALLOC);
    MAP(ND_FP_TO_INT, PSX_HIR_FP_TO_INT);
    MAP(ND_INT_TO_FP, PSX_HIR_INT_TO_FP);
    MAP(ND_VARARG_CURSOR, PSX_HIR_VARARG_CURSOR);
    MAP(ND_CAST, PSX_HIR_CAST);
    MAP(ND_CREAL, PSX_HIR_CREAL);
    MAP(ND_CIMAG, PSX_HIR_CIMAG);
    MAP(ND_STMT_EXPR, PSX_HIR_STMT_EXPR);
    default:
      return 0;
  }
#undef MAP
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

static psx_semantic_node_t *build_node(
    hir_materializer_t *builder, const node_t *source);
static int canonical_type_exists(
    const hir_materializer_t *builder, psx_qual_type_t type);
static psx_qual_type_t resolved_expression_type(
    const psx_semantic_node_t *node);
static psx_semantic_node_t *materialize_cast_expression(
    hir_materializer_t *builder, const node_t *source,
    psx_semantic_node_t *operand, psx_qual_type_t qual_type);
static psx_semantic_node_t *materialize_statement(
    hir_materializer_t *builder, const psx_hir_node_spec_t *spec,
    const hir_children_t *children, const node_t *source);

static psx_semantic_node_t *materialize_vla_runtime(
    hir_materializer_t *builder,
    const node_vla_alloc_t *allocation) {
  const node_t *source = allocation
                             ? &allocation->base : NULL;
  const psx_vla_runtime_plan_t *plan =
      allocation ? allocation->runtime_plan : NULL;
  return psx_semantic_node_builder_vla_runtime(
      builder, plan,
      psx_resolution_node_kind(
          materializer_resolution_store(builder), source));
}

static psx_semantic_node_t *materialize_expression_spec(
    hir_materializer_t *builder, const psx_hir_node_spec_t *spec,
    psx_qual_type_t qual_type, const hir_children_t *children,
    const psx_hir_symbol_spec_t *symbol, const node_t *source) {
  return psx_semantic_node_builder_expression(
      builder, spec, qual_type,
      children ? children->items : NULL,
      children ? children->edges : NULL,
      children ? children->count : 0,
      symbol, psx_resolution_node_kind(
                  materializer_resolution_store(builder), source));
}

static psx_semantic_node_t *materialize_expression(
    hir_materializer_t *builder, psx_hir_node_kind_t kind,
    psx_qual_type_t qual_type, const hir_children_t *children,
    const node_t *source) {
  psx_hir_node_spec_t spec = {
      .kind = kind,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
  };
  return materialize_expression_spec(
      builder, &spec, qual_type, children, NULL, source);
}

static psx_semantic_node_t *materialize_statement(
    hir_materializer_t *builder, const psx_hir_node_spec_t *spec,
    const hir_children_t *children, const node_t *source) {
  return psx_semantic_node_builder_statement(
      builder, spec,
      children ? children->items : NULL,
      children ? children->edges : NULL,
      children ? children->count : 0,
      psx_resolution_node_kind(
          materializer_resolution_store(builder), source));
}

static psx_semantic_node_t *materialize_comma(
    hir_materializer_t *builder,
    psx_semantic_node_t *lhs, psx_semantic_node_t *rhs,
    psx_qual_type_t qual_type, const node_t *source) {
  psx_semantic_node_t *items[] = {lhs, rhs};
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
    psx_semantic_node_t **prefix, const node_t *source) {
  if (!operand || operand->kind != ND_SUBSCRIPT) return 1;
  if (!materialize_sizeof_vla_indices(
          builder, operand->lhs, prefix, source))
    return 0;
  psx_semantic_node_t *index =
      build_node(builder, operand->rhs);
  if (!index) return 0;
  if (!*prefix) {
    *prefix = index;
    return 1;
  }
  *prefix = materialize_comma(
      builder, *prefix, index, resolved_expression_type(index), source);
  return *prefix != NULL;
}

static psx_semantic_node_t *materialize_sizeof_value(
    hir_materializer_t *builder, const node_sizeof_query_t *query,
    psx_qual_type_t qual_type) {
  const psx_sizeof_runtime_plan_t *plan =
      psx_sizeof_query_runtime_plan_const(
          materializer_resolution_store(builder), query);
  if (plan) {
    psx_hir_node_spec_t number_spec = {
        .kind = PSX_HIR_NUMBER,
        .attached_qual_type = {
            PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
        .integer_value = plan->constant_factor,
    };
    psx_semantic_node_t *value = materialize_expression_spec(
        builder, &number_spec, qual_type, NULL, NULL, &query->base);
    if (!value) return NULL;
    for (int i = 0; i < plan->runtime_bound_count; i++) {
      node_t *bound_source = plan->runtime_bounds[i];
      psx_semantic_node_t *bound =
          build_node(builder, bound_source);
      if (!bound) return NULL;
      bound = materialize_cast_expression(
          builder, bound_source, bound, qual_type);
      if (!bound) return NULL;
      psx_semantic_node_t *items[] = {value, bound};
      psx_hir_edge_kind_t edges[] = {
          PSX_HIR_EDGE_LHS, PSX_HIR_EDGE_RHS};
      hir_children_t children = {
          .items = items,
          .edges = edges,
          .count = 2,
          .capacity = 2,
      };
      value = materialize_expression(
          builder, PSX_HIR_MUL, qual_type, &children, &query->base);
      if (!value) return NULL;
    }
    return value;
  }
  int runtime_size_slot =
      psx_sizeof_query_runtime_size_slot(
          materializer_resolution_store(builder), query);
  if (runtime_size_slot != 0) {
    psx_hir_node_spec_t spec = {
        .kind = PSX_HIR_LOCAL,
        .attached_qual_type = {
            PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
        .storage_offset = runtime_size_slot,
        .object_offset = runtime_size_slot,
        .object_size = PSX_VLA_RUNTIME_SLOT_SIZE,
        .object_align = PSX_VLA_RUNTIME_SLOT_SIZE,
    };
    return materialize_expression_spec(
        builder, &spec, qual_type, NULL, NULL, &query->base);
  }
  psx_hir_node_spec_t spec = {
      .kind = PSX_HIR_NUMBER,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
      .integer_value = psx_sizeof_query_resolved_size(
          materializer_resolution_store(builder), query),
  };
  return materialize_expression_spec(
      builder, &spec, qual_type, NULL, NULL, &query->base);
}

static psx_semantic_node_t *materialize_sizeof_query(
    hir_materializer_t *builder, const node_sizeof_query_t *query) {
  const node_t *source = &query->base;
  psx_qual_type_t qual_type = ps_node_qual_type(
      materializer_resolution_store(builder), source);
  if (!canonical_type_exists(builder, qual_type)) {
    set_failure(
        builder, PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE, source);
    return NULL;
  }
  psx_semantic_node_t *value =
      materialize_sizeof_value(builder, query, qual_type);
  if (!value) return NULL;
  if (!psx_sizeof_query_evaluates_vla_operand(
          materializer_resolution_store(builder), query))
    return value;
  psx_semantic_node_t *prefix = NULL;
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

static psx_qual_type_t resolved_expression_type(
    const psx_semantic_node_t *node) {
  return psx_semantic_node_expression_qual_type(node);
}

static psx_semantic_node_t *build_statement_expression_prefix(
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
  psx_semantic_node_t *result = materialize_statement(
      builder, &spec, &children, source);
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
  switch (psx_resolved_object_ref_node_kind(
      materializer_resolution_store(builder), source)) {
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
    case ND_STATIC_ASSERT:
      *include_common_children = 0;
      return 1;
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
    case ND_CASE:
      *include_common_children = 0;
      return build_and_append(
          builder, children, source->rhs, PSX_HIR_EDGE_RHS);
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
  return psx_semantic_node_builder_has_canonical_type(
      builder, type);
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
    const psx_semantic_node_t *child = children->items[i];
    if (child) return resolved_expression_type(child);
  }
  return (psx_qual_type_t){
      PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
}

static int derive_structural_expression_type(
    hir_materializer_t *builder, const node_t *source,
    const hir_children_t *children, psx_qual_type_t *qual_type) {
  psx_qual_type_t derived = {
      PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  switch (psx_resolved_object_ref_node_kind(
      materializer_resolution_store(builder), source)) {
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
  if (!psx_resolve_global_hir_symbol_spec_in(
          builder->semantic_context, global, symbol)) {
    set_failure(
        builder, PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE, source);
    return 0;
  }
  return 1;
}

static int attach_global_symbol(
    hir_materializer_t *builder, const node_t *source,
    psx_hir_symbol_spec_t *symbol, int *has_symbol) {
  if (has_symbol) *has_symbol = 0;
  if (psx_resolved_object_ref_node_kind(
          materializer_resolution_store(builder), source) != ND_GVAR)
    return 1;
  const global_var_t *global =
      psx_resolved_object_ref_global(
          materializer_resolution_store(builder), source);
  if (!resolved_global_symbol_spec(
          builder, global, source, symbol))
    return 0;
  if (has_symbol) *has_symbol = 1;
  return 1;
}

static psx_semantic_node_t *materialize_local_object_reference(
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
  psx_hir_node_spec_t spec = {0};
  if (!psx_resolve_local_hir_node_spec_in(
          builder->semantic_context, local,
          ps_lvar_offset(local), &spec)) {
    set_failure(
        builder, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY, source);
    return NULL;
  }
  return materialize_expression_spec(
      builder, &spec, qual_type, NULL, NULL, source);
}

static psx_semantic_node_t *materialize_local_subobject_reference(
    hir_materializer_t *builder, const lvar_t *local,
    int relative_offset, psx_qual_type_t qual_type,
    int bit_width, int bit_offset, int bit_is_signed,
    const node_t *source) {
  if (!local) {
    set_failure(
        builder, PSX_RESOLVED_HIR_BUILD_MISSING_RESOLVED_SYMBOL, source);
    return NULL;
  }
  if (!canonical_type_exists(builder, qual_type)) {
    set_failure(
        builder, PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE, source);
    return NULL;
  }
  psx_hir_node_spec_t spec = {
      .kind = PSX_HIR_LOCAL,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
      .storage_offset = ps_lvar_offset(local) + relative_offset,
      .object_offset = ps_lvar_offset(local),
      .object_size = ps_lvar_frame_storage_size(local),
      .object_align = ps_lvar_align_bytes(local),
      .bit_width = (unsigned char)(bit_width > 0 ? bit_width : 0),
      .bit_offset = (unsigned char)(bit_offset > 0 ? bit_offset : 0),
      .bit_is_signed = bit_is_signed ? 1 : 0,
  };
  return materialize_expression_spec(
      builder, &spec, qual_type, NULL, NULL, source);
}

static psx_semantic_node_t *materialize_global_object_reference(
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
  return materialize_expression_spec(
      builder, &spec, ps_gvar_decl_qual_type(global),
      NULL, &symbol, source);
}

static psx_semantic_node_t *materialize_runtime_initializer_value(
    hir_materializer_t *builder,
    const psx_runtime_initializer_value_t *value,
    const node_t *source) {
  if (!value) return NULL;
  switch (value->kind) {
    case PSX_RUNTIME_INITIALIZER_VALUE_EXPRESSION:
      return build_node(builder, value->expression);
    case PSX_RUNTIME_INITIALIZER_VALUE_LOCAL:
      return materialize_local_subobject_reference(
          builder, value->local.local, value->local.relative_offset,
          value->local.qual_type, value->local.bit_width,
          value->local.bit_offset, value->local.bit_is_signed, source);
    case PSX_RUNTIME_INITIALIZER_VALUE_NUMBER: {
      if (!canonical_type_exists(builder, value->resolved_qual_type)) {
        set_failure(
            builder, PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE, source);
        return NULL;
      }
      psx_hir_node_spec_t spec = {
          .kind = PSX_HIR_NUMBER,
          .integer_value = value->number.integer_value,
          .floating_value = value->number.floating_value,
      };
      return materialize_expression_spec(
          builder, &spec, value->resolved_qual_type,
          NULL, NULL, source);
    }
  }
  return NULL;
}

static psx_semantic_node_t *materialize_runtime_initializer_item(
    hir_materializer_t *builder,
    const psx_runtime_initializer_item_t *item,
    const node_t *source) {
  if (!item) return NULL;
  psx_semantic_node_t *value =
      materialize_runtime_initializer_value(
          builder, &item->value, source);
  if (!value ||
      item->kind == PSX_RUNTIME_INITIALIZER_EVALUATE)
    return value;
  psx_semantic_node_t *target =
      materialize_local_subobject_reference(
          builder, item->target.local, item->target.relative_offset,
          item->target.qual_type, item->target.bit_width,
          item->target.bit_offset, item->target.bit_is_signed, source);
  if (!target) return NULL;
  psx_semantic_node_t *items[] = {target, value};
  psx_hir_edge_kind_t edges[] = {
      PSX_HIR_EDGE_LHS, PSX_HIR_EDGE_RHS};
  hir_children_t children = {
      .items = items,
      .edges = edges,
      .count = 2,
      .capacity = 2,
  };
  return materialize_expression(
      builder, PSX_HIR_ASSIGN, item->target.qual_type,
      &children, source);
}

static psx_semantic_node_t *materialize_runtime_initializer(
    hir_materializer_t *builder,
    const psx_runtime_initializer_plan_t *plan,
    const node_t *source) {
  if (!plan || plan->item_count <= 0) return NULL;
  psx_semantic_node_t *sequence = NULL;
  for (int i = 0; i < plan->item_count; i++) {
    const psx_runtime_initializer_item_t *item = &plan->items[i];
    psx_semantic_node_t *current =
        materialize_runtime_initializer_item(builder, item, source);
    if (!current) return NULL;
    sequence = sequence
                   ? materialize_comma(
                         builder, sequence, current,
                         item->result_qual_type, source)
                   : current;
    if (!sequence) return NULL;
  }
  return sequence;
}

static psx_semantic_node_t *materialize_address_of_object(
    hir_materializer_t *builder,
    psx_semantic_node_t *object,
    psx_qual_type_t result_type, const node_t *source) {
  psx_semantic_node_t *items[] = {object};
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

static psx_semantic_node_t *materialize_compound_literal(
    hir_materializer_t *builder,
    const node_compound_literal_t *compound) {
  const node_t *source = &compound->base;
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(
          materializer_resolution_store(builder), source);
  const psx_compound_literal_resolution_t *resolution =
      state ? &state->compound_literal : NULL;
  psx_qual_type_t result_type = ps_node_qual_type(
      materializer_resolution_store(builder), source);
  if (!resolution ||
      resolution->kind == PSX_COMPOUND_LITERAL_UNPLANNED) {
    set_failure(
        builder, PSX_RESOLVED_HIR_BUILD_RAW_SYNTAX_REMAINS, source);
    return NULL;
  }
  if (!canonical_type_exists(builder, result_type)) {
    set_failure(
        builder, PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE, source);
    return NULL;
  }
  const node_t *direct =
      psx_compound_literal_direct_initializer_const(
          materializer_resolution_store(builder), compound);
  if (direct) return build_node(builder, direct);

  psx_semantic_node_t *object =
      resolution->local_object
          ? materialize_local_object_reference(
                builder, resolution->local_object, source)
          : materialize_global_object_reference(
                builder, resolution->global_object, source);
  if (!object) return NULL;
  psx_semantic_node_t *value = object;
  if (!value) return NULL;
  if (!resolution->runtime_initializer) return value;
  psx_semantic_node_t *initialization =
      materialize_runtime_initializer(
          builder, resolution->runtime_initializer, source);
  if (!initialization) return NULL;
  return materialize_comma(
      builder, initialization, value, result_type, source);
}

static int source_cast_is_aggregate(
    const psx_resolution_store_t *store,
    const node_source_cast_t *cast) {
  psx_source_cast_resolution_kind_t kind =
      psx_source_cast_resolution_kind(store, cast);
  return kind == PSX_SOURCE_CAST_AGGREGATE_DIRECT_HIR ||
         kind == PSX_SOURCE_CAST_AGGREGATE_TEMPORARY;
}

static int address_requires_typed_hir_lowering(
    const psx_resolution_store_t *store, const node_t *source) {
  return source && source->kind == ND_ADDR && source->lhs &&
         source->lhs->kind == ND_CAST &&
         source->lhs->is_source_cast &&
         source_cast_is_aggregate(
             store,
             (const node_source_cast_t *)source->lhs);
}

static psx_semantic_node_t *materialize_cast_expression(
    hir_materializer_t *builder, const node_t *source,
    psx_semantic_node_t *operand, psx_qual_type_t qual_type) {
  psx_semantic_node_t *items[] = {operand};
  psx_hir_edge_kind_t edges[] = {PSX_HIR_EDGE_LHS};
  hir_children_t children = {
      .items = items,
      .edges = edges,
      .count = 1,
      .capacity = 1,
  };
  return materialize_expression(
      builder, PSX_HIR_CAST, qual_type, &children, source);
}

static int materialize_aggregate_temporary_parts(
    hir_materializer_t *builder, const node_source_cast_t *cast,
    psx_semantic_node_t **initialization,
    psx_semantic_node_t **object) {
  const node_t *source = &cast->base;
  const psx_source_cast_resolution_t *resolution =
      psx_source_cast_resolution_state(
          materializer_resolution_store(builder), cast);
  if (!resolution ||
      resolution->kind != PSX_SOURCE_CAST_AGGREGATE_TEMPORARY ||
      !resolution->aggregate_temporary ||
      !initialization || !object)
    return 0;
  psx_semantic_node_t *target =
      materialize_local_subobject_reference(
          builder, resolution->aggregate_temporary,
          resolution->aggregate_member_offset,
          resolution->aggregate_member_qual_type,
          resolution->aggregate_member_bit_width,
          resolution->aggregate_member_bit_offset,
          resolution->aggregate_member_bit_is_signed,
          source);
  psx_semantic_node_t *value =
      build_node(builder, source->lhs);
  *object = materialize_local_object_reference(
      builder, resolution->aggregate_temporary, source);
  if (!target || !value || !*object) return 0;
  psx_semantic_node_t *items[] = {target, value};
  psx_hir_edge_kind_t edges[] = {
      PSX_HIR_EDGE_LHS, PSX_HIR_EDGE_RHS};
  hir_children_t children = {
      .items = items,
      .edges = edges,
      .count = 2,
      .capacity = 2,
  };
  *initialization = materialize_expression(
      builder, PSX_HIR_ASSIGN,
      resolution->aggregate_member_qual_type, &children, source);
  return *initialization != NULL;
}

static psx_semantic_node_t *materialize_aggregate_cast_address(
    hir_materializer_t *builder, const node_source_cast_t *cast,
    psx_qual_type_t result_type, const node_t *address_source) {
  psx_source_cast_resolution_kind_t kind =
      psx_source_cast_resolution_kind(
          materializer_resolution_store(builder), cast);
  if (kind == PSX_SOURCE_CAST_AGGREGATE_DIRECT_HIR) {
    if (cast->base.lhs &&
        cast->base.lhs->kind == ND_CAST &&
        cast->base.lhs->is_source_cast &&
        source_cast_is_aggregate(
            materializer_resolution_store(builder),
            (const node_source_cast_t *)cast->base.lhs)) {
      return materialize_aggregate_cast_address(
          builder, (const node_source_cast_t *)cast->base.lhs,
          result_type, address_source);
    }
    psx_semantic_node_t *object =
        build_node(builder, cast->base.lhs);
    return object
               ? materialize_address_of_object(
                     builder, object, result_type, address_source)
               : NULL;
  }
  if (kind == PSX_SOURCE_CAST_AGGREGATE_TEMPORARY) {
    psx_semantic_node_t *initialization = NULL;
    psx_semantic_node_t *object = NULL;
    if (!materialize_aggregate_temporary_parts(
            builder, cast, &initialization, &object))
      return NULL;
    psx_semantic_node_t *address =
        materialize_address_of_object(
            builder, object, result_type, address_source);
    return address
               ? materialize_comma(
                     builder, initialization, address,
                     result_type, address_source)
               : NULL;
  }
  set_failure(
      builder, PSX_RESOLVED_HIR_BUILD_RAW_SYNTAX_REMAINS,
      address_source);
  return NULL;
}

static psx_semantic_node_t *materialize_address_expression(
    hir_materializer_t *builder, const node_t *source) {
  psx_qual_type_t result_type = ps_node_qual_type(
      materializer_resolution_store(builder), source);
  if (!canonical_type_exists(builder, result_type)) {
    set_failure(
        builder, PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE, source);
    return NULL;
  }
  return materialize_aggregate_cast_address(
      builder, (const node_source_cast_t *)source->lhs,
      result_type, source);
}

static psx_semantic_node_t *materialize_source_cast(
    hir_materializer_t *builder, const node_source_cast_t *cast) {
  const node_t *source = &cast->base;
  psx_qual_type_t qual_type = ps_node_qual_type(
      materializer_resolution_store(builder), source);
  if (!canonical_type_exists(builder, qual_type)) {
    set_failure(
        builder, PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE, source);
    return NULL;
  }
  psx_source_cast_resolution_kind_t resolution_kind =
      psx_source_cast_resolution_kind(
          materializer_resolution_store(builder), cast);
  if (!source->lhs ||
      (resolution_kind != PSX_SOURCE_CAST_DIRECT_HIR &&
       resolution_kind != PSX_SOURCE_CAST_AGGREGATE_DIRECT_HIR &&
       resolution_kind != PSX_SOURCE_CAST_AGGREGATE_TEMPORARY)) {
    set_failure(
        builder, PSX_RESOLVED_HIR_BUILD_RAW_SYNTAX_REMAINS, source);
    return NULL;
  }
  if (resolution_kind == PSX_SOURCE_CAST_AGGREGATE_TEMPORARY) {
    psx_semantic_node_t *initialization = NULL;
    psx_semantic_node_t *object = NULL;
    if (!materialize_aggregate_temporary_parts(
            builder, cast, &initialization, &object))
      return NULL;
    return materialize_comma(
        builder, initialization, object, qual_type, source);
  }
  psx_semantic_node_t *operand =
      build_node(builder, source->lhs);
  if (!operand) return NULL;
  return materialize_cast_expression(
      builder, source, operand, qual_type);
}

static int copy_payload(
    hir_materializer_t *builder, const node_t *source,
    psx_hir_node_spec_t *spec) {
  switch (psx_resolved_object_ref_node_kind(
      materializer_resolution_store(builder), source)) {
    case ND_ASSIGN:
      if (source->is_source_compound_assignment) {
        psx_hir_compound_operator_t op = PSX_HIR_COMPOUND_ADD;
        if (compound_operator(source->source_op, &op))
          spec->integer_value = op;
      }
      break;
    case ND_ALIGNOF_QUERY:
      spec->integer_value =
          psx_alignof_query_resolved_alignment(
              materializer_resolution_store(builder),
              (const node_alignof_query_t *)source);
      break;
    case ND_NUM: {
      const node_num_t *number = (const node_num_t *)source;
      spec->integer_value = number->val;
      spec->floating_value = number->fval;
      break;
    }
    case ND_LVAR: {
      const lvar_t *local =
          psx_resolved_object_ref_local(
              materializer_resolution_store(builder), source);
      int storage_offset =
          psx_resolved_object_ref_storage_offset(
              materializer_resolution_store(builder), source);
      if (local) {
        if (!psx_resolve_local_hir_node_spec_in(
                builder->semantic_context, local,
                storage_offset, spec)) {
          set_failure(
              builder, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY, source);
          return 0;
        }
      } else {
        spec->storage_offset = storage_offset;
        spec->object_offset = storage_offset;
      }
      break;
    }
    case ND_MEMBER_ACCESS: {
      const node_member_access_t *access =
          (const node_member_access_t *)source;
      const psx_member_access_state_t *state =
          psx_member_access_state(
              materializer_resolution_store(builder), access);
      if (!state || !state->is_resolved) {
        set_failure(
            builder, PSX_RESOLVED_HIR_BUILD_RAW_SYNTAX_REMAINS, source);
        return 0;
      }
      const psx_record_layout_t *layout =
          psx_record_layout_table_lookup(
              ps_ctx_record_layout_table_in(builder->semantic_context),
              state->record_id,
              ps_ctx_target_info(builder->semantic_context));
      const psx_record_member_layout_t *member =
          psx_record_layout_member(layout, state->member_index);
      if (!member) {
        set_failure(
            builder, PSX_RESOLVED_HIR_BUILD_RAW_SYNTAX_REMAINS, source);
        return 0;
      }
      spec->member_offset = member->offset;
      spec->member_from_pointer = access->from_pointer ? 1 : 0;
      if (state->declaration.bit_width > 0) {
        spec->bit_width =
            (unsigned char)state->declaration.bit_width;
        spec->bit_offset = (unsigned char)member->bit_offset;
        spec->bit_is_signed =
            state->declaration.bit_is_signed ? 1 : 0;
      }
      break;
    }
    case ND_STRING: {
      const node_string_t *string = (const node_string_t *)source;
      char *string_label = psx_string_literal_label(
          materializer_resolution_store(builder), string);
      spec->name = string_label;
      spec->name_length = string_label ? strlen(string_label) : 0;
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
      int direct_name_len =
          psx_function_call_direct_name_length(
              materializer_resolution_store(builder), call);
      spec->name = psx_function_call_direct_name(
          materializer_resolution_store(builder), call);
      spec->name_length = direct_name_len > 0
                              ? (size_t)direct_name_len : 0;
      spec->attached_qual_type = psx_function_call_qual_type(
          materializer_resolution_store(builder), call);
      spec->is_implicit_call =
          psx_function_call_is_implicit_declaration(
              materializer_resolution_store(builder), call)
              ? 1 : 0;
      break;
    }
    case ND_FUNCREF: {
      int name_len = 0;
      spec->name = psx_resolved_object_ref_name(
          materializer_resolution_store(builder), source, &name_len);
      spec->name_length = name_len > 0 ? (size_t)name_len : 0;
      break;
    }
    case ND_GVAR: {
      int name_len = 0;
      spec->name = psx_resolved_object_ref_name(
          materializer_resolution_store(builder), source, &name_len);
      spec->name_length = name_len > 0 ? (size_t)name_len : 0;
      break;
    }
    case ND_CASE: {
      const node_case_t *case_node = (const node_case_t *)source;
      if (!psx_case_label_is_resolved(
              materializer_resolution_store(builder), case_node)) {
        set_failure(
            builder, PSX_RESOLVED_HIR_BUILD_RAW_SYNTAX_REMAINS, source);
        return 0;
      }
      spec->integer_value = psx_case_label_value(
          materializer_resolution_store(builder), case_node);
      break;
    }
    case ND_DEFAULT:
      break;
    case ND_GOTO:
    case ND_LABEL: {
      const node_jump_t *jump = (const node_jump_t *)source;
      spec->name = jump->name;
      spec->name_length = jump->name_len > 0
                              ? (size_t)jump->name_len : 0;
      break;
    }
    case ND_VLA_ALLOC: {
      const psx_vla_runtime_plan_t *plan =
          ((const node_vla_alloc_t *)source)->runtime_plan;
      if (!plan) return 0;
      spec->storage_offset =
          plan->descriptor_frame_offset;
      spec->vla_stride_frame_offset =
          plan->row_stride_frame_offset;
      spec->vla_stride_element_size = plan->element_size;
      spec->vla_stride_slot_size = PSX_VLA_RUNTIME_SLOT_SIZE;
      break;
    }
    default:
      break;
  }
  return 1;
}

static int copy_vla_payload(
    const hir_materializer_t *builder,
    const node_t *source, psx_hir_node_spec_t *spec) {
  if (source->kind == ND_SUBSCRIPT) {
    spec->vla_stride_frame_offset =
        ps_node_vla_row_stride_frame_off(
            materializer_resolution_store(builder), (node_t *)source);
    if (spec->vla_stride_frame_offset != 0)
      spec->vla_stride_slot_size = PSX_VLA_RUNTIME_SLOT_SIZE;
    return 1;
  }
  return 1;
}

static psx_semantic_node_t *materialize_typed_leaf(
    hir_materializer_t *builder, const node_t *source,
    int *handled) {
  if (handled) *handled = 0;
  if (!source || !handled) return NULL;
  psx_hir_node_kind_t kind;
  switch (psx_resolved_object_ref_node_kind(
      materializer_resolution_store(builder), source)) {
    case ND_NUM: kind = PSX_HIR_NUMBER; break;
    case ND_STRING: kind = PSX_HIR_STRING; break;
    case ND_LVAR: kind = PSX_HIR_LOCAL; break;
    case ND_GVAR: kind = PSX_HIR_GLOBAL; break;
    case ND_FUNCREF: kind = PSX_HIR_FUNCTION_REF; break;
    default: return NULL;
  }
  *handled = 1;
  psx_hir_node_spec_t spec = {
      .kind = kind,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
  };
  if (!copy_payload(builder, source, &spec) ||
      !copy_vla_payload(builder, source, &spec))
    return NULL;
  if (psx_resolved_object_ref_node_kind(
          materializer_resolution_store(builder), source) == ND_LVAR) {
    int bit_width = 0;
    int bit_offset = 0;
    int bit_is_signed = 0;
    if (ps_node_bitfield_info(
            materializer_resolution_store(builder), (node_t *)source,
            &bit_width, &bit_offset,
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
  return psx_semantic_node_builder_leaf_expression(
      builder, &spec, ps_node_qual_type(
                          materializer_resolution_store(builder), source),
      has_symbol ? &symbol : NULL,
      psx_resolution_node_kind(
          materializer_resolution_store(builder), source));
}

static psx_semantic_node_t *materialize_initializer_designator(
    hir_materializer_t *builder,
    const psx_initializer_designator_t *designator,
    const node_t *source) {
  if (!designator || !source) return NULL;
  psx_hir_node_spec_t spec = {
      .kind = designator->kind == PSX_INIT_DESIGNATOR_MEMBER
                  ? PSX_HIR_MEMBER_DESIGNATOR
                  : PSX_HIR_INDEX_DESIGNATOR,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
      .integer_value = designator->is_range ? 1 : 0,
  };
  hir_children_t children = {0};
  if (designator->kind == PSX_INIT_DESIGNATOR_MEMBER) {
    spec.name = designator->member_name;
    spec.name_length = designator->member_len > 0
                           ? (size_t)designator->member_len : 0;
  } else if (!designator->index_expr ||
             !append_child(
                 builder, &children,
                 build_node(builder, designator->index_expr),
                 PSX_HIR_EDGE_DESIGNATOR_INDEX, source) ||
             (designator->range_end_expr &&
              !append_child(
                  builder, &children,
                  build_node(builder, designator->range_end_expr),
                  PSX_HIR_EDGE_DESIGNATOR_RANGE_END, source))) {
    free(children.items);
    free(children.edges);
    return NULL;
  }
  psx_semantic_node_t *result = materialize_statement(
      builder, &spec, &children, source);
  free(children.items);
  free(children.edges);
  return result;
}

static psx_semantic_node_t *materialize_initializer_entry(
    hir_materializer_t *builder,
    const psx_initializer_entry_t *entry,
    const node_t *source) {
  if (!entry || !entry->value || !source) return NULL;
  hir_children_t children = {0};
  for (int i = 0; i < entry->designator_count; i++) {
    if (!append_child(
            builder, &children,
            materialize_initializer_designator(
                builder, &entry->designators[i], source),
            PSX_HIR_EDGE_DESIGNATOR, source)) {
      free(children.items);
      free(children.edges);
      return NULL;
    }
  }
  if (!append_child(
          builder, &children, build_node(builder, entry->value),
          PSX_HIR_EDGE_INITIALIZER_VALUE, source)) {
    free(children.items);
    free(children.edges);
    return NULL;
  }
  psx_hir_node_spec_t spec = {
      .kind = PSX_HIR_INITIALIZER_ENTRY,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
  };
  psx_semantic_node_t *result = materialize_statement(
      builder, &spec, &children, source);
  free(children.items);
  free(children.edges);
  return result;
}

static psx_semantic_node_t *materialize_initializer_list(
    hir_materializer_t *builder, const node_init_list_t *list) {
  if (!list) return NULL;
  const node_t *source = &list->base;
  hir_children_t children = {0};
  for (int i = 0; i < list->entry_count; i++) {
    if (!append_child(
            builder, &children,
            materialize_initializer_entry(
                builder, &list->entries[i], source),
            PSX_HIR_EDGE_INITIALIZER_ENTRY, source)) {
      free(children.items);
      free(children.edges);
      return NULL;
    }
  }
  psx_hir_node_spec_t spec = {
      .kind = PSX_HIR_INITIALIZER_LIST,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
  };
  psx_semantic_node_t *result = materialize_statement(
      builder, &spec, &children, source);
  free(children.items);
  free(children.edges);
  return result;
}

static psx_semantic_node_t *build_node(
    hir_materializer_t *builder, const node_t *source) {
  int handled_typed_leaf = 0;
  psx_semantic_node_t *typed_leaf = materialize_typed_leaf(
      builder, source, &handled_typed_leaf);
  if (handled_typed_leaf) return typed_leaf;
  psx_resolution_node_kind_t resolved_kind =
      psx_resolution_node_kind(
          materializer_resolution_store(builder), source);
  if (resolved_kind == ND_INIT_LIST) {
    return materialize_initializer_list(
        builder, (const node_init_list_t *)source);
  }
  if (resolved_kind == ND_VLA_ALLOC) {
    return materialize_vla_runtime(
        builder, (const node_vla_alloc_t *)source);
  }
  if (address_requires_typed_hir_lowering(
          materializer_resolution_store(builder), source))
    return materialize_address_expression(builder, source);
  if (resolved_kind == ND_COMPOUND_LITERAL) {
    return materialize_compound_literal(
        builder, (const node_compound_literal_t *)source);
  }
  if (resolved_kind == ND_SIZEOF_QUERY) {
    return materialize_sizeof_query(
        builder, (const node_sizeof_query_t *)source);
  }
  if (resolved_kind == ND_GENERIC_SELECTION) {
    const node_generic_selection_t *selection =
        (const node_generic_selection_t *)source;
    psx_qual_type_t qual_type = ps_node_qual_type(
        materializer_resolution_store(builder), source);
    const node_t *selected =
        psx_generic_selection_selected_expression_const(
            materializer_resolution_store(builder), selection);
    if (!canonical_type_exists(builder, qual_type)) {
      set_failure(
          builder, PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE, source);
      return NULL;
    }
    if (!selected) {
      set_failure(
          builder, PSX_RESOLVED_HIR_BUILD_RAW_SYNTAX_REMAINS, source);
      return NULL;
    }
    return build_node(builder, selected);
  }
  if (resolved_kind == ND_CAST && source->is_source_cast) {
    return materialize_source_cast(
        builder, (const node_source_cast_t *)source);
  }
  psx_hir_node_spec_t spec = {0};
  psx_qual_type_t qual_type = {
      PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  spec.attached_qual_type = qual_type;
  if (!map_kind(resolved_kind, &spec.kind)) {
    set_failure(builder, PSX_RESOLVED_HIR_BUILD_RAW_SYNTAX_REMAINS, source);
    return NULL;
  }
  if (resolved_kind == ND_ASSIGN &&
      source->is_source_compound_assignment)
    spec.kind = PSX_HIR_COMPOUND_ASSIGN;
  int is_expression = psx_hir_kind_is_expression(spec.kind);
  if (is_expression) {
    qual_type = ps_node_qual_type(
        materializer_resolution_store(builder), source);
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
    if (resolved_kind != ND_MEMBER_ACCESS &&
        ps_node_bitfield_info(
            materializer_resolution_store(builder), (node_t *)source,
            &bit_width, &bit_offset,
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
  if (resolved_kind == ND_FUNCDEF &&
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
            resolved_kind == ND_FUNCDEF
                ? PSX_HIR_EDGE_FUNCTION_BODY : PSX_HIR_EDGE_RHS)))) {
    free(children.items);
    free(children.edges);
    return NULL;
  }
  if (is_expression &&
      !derive_structural_expression_type(
          builder, source, &children, &qual_type)) {
    free(children.items);
    free(children.edges);
    return NULL;
  }
  psx_semantic_node_t *result =
      is_expression
          ? materialize_expression_spec(
                builder, &spec, qual_type, &children,
                has_symbol ? &symbol : NULL, source)
          : materialize_statement(
                builder, &spec, &children, source);
  free(children.items);
  free(children.edges);
  return result;
}

psx_typed_hir_tree_t *psx_typed_hir_tree_materialize(
    const node_t *resolution_root,
    const psx_semantic_context_t *semantic_context,
    psx_resolved_hir_build_failure_t *failure) {
  if (failure) memset(failure, 0, sizeof(*failure));
  if (!semantic_context || !resolution_root) {
    if (failure) {
      failure->status = PSX_RESOLVED_HIR_BUILD_INVALID_INPUT;
      failure->source_node_kind =
          psx_resolution_node_kind(
              ps_ctx_resolution_store(semantic_context), resolution_root);
    }
    return NULL;
  }
  hir_materializer_t builder;
  psx_semantic_node_builder_init(
      &builder, ps_ctx_arena(semantic_context),
      semantic_context, failure);
  psx_semantic_node_t *root = build_node(&builder, resolution_root);
  if (!root) {
    if (failure && failure->status == PSX_RESOLVED_HIR_BUILD_OK)
      failure->status = PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY;
    return NULL;
  }
  psx_typed_hir_tree_t *tree = arena_alloc_in(
      ps_ctx_arena(semantic_context), sizeof(*tree));
  if (!tree) {
    if (failure && failure->status == PSX_RESOLVED_HIR_BUILD_OK)
      failure->status = PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY;
    return NULL;
  }
  tree->root = root;
  return tree;
}
