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
#include "../lowering/runtime_initializer_plan.h"
#include "compound_literal_resolution.h"
#include "alignof_query_resolution.h"
#include "typed_hir_node_internal.h"
#include "generic_selection_resolution.h"
#include "member_access_resolution.h"
#include "resolved_node_kind.h"
#include "resolved_node.h"
#include "resolved_function.h"
#include "sizeof_query_resolution.h"
#include "source_cast_resolution.h"
#include "typed_hir_tree_internal.h"
#include "resolution_work_tree_internal.h"
#include "vla_runtime_plan.h"

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
    psx_work_node_kind_t source, psx_hir_node_kind_t *kind) {
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
    MAP(ND_LVAR, PSX_HIR_LOCAL);
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
    MAP(ND_FUNCREF, PSX_HIR_FUNCTION_REF);
    MAP(ND_UNARY_NEGATE, PSX_HIR_NEGATE);
    MAP(ND_UNARY_DEREF, PSX_HIR_DEREF);
    MAP(ND_DEREF, PSX_HIR_DEREF);
    MAP(ND_SUBSCRIPT, PSX_HIR_SUBSCRIPT);
    MAP(ND_MEMBER_ACCESS, PSX_HIR_MEMBER_ACCESS);
    MAP(ND_ALIGNOF_QUERY, PSX_HIR_NUMBER);
    MAP(ND_ADDR, PSX_HIR_ADDRESS);
    MAP(ND_STRING, PSX_HIR_STRING);
    MAP(ND_NUM, PSX_HIR_NUMBER);
    MAP(ND_GVAR, PSX_HIR_GLOBAL);
    MAP(ND_VLA_ALLOC, PSX_HIR_VLA_ALLOC);
    MAP(ND_FP_TO_INT, PSX_HIR_FP_TO_INT);
    MAP(ND_INT_TO_FP, PSX_HIR_INT_TO_FP);
    MAP(ND_VA_ARG_AREA, PSX_HIR_VA_ARG_AREA);
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

static psx_resolved_hir_node_t *build_node(
    hir_materializer_t *builder, const node_t *source);
static psx_resolved_hir_node_t *materialize_node_record(
    hir_materializer_t *builder, const psx_hir_node_spec_t *spec,
    const hir_children_t *children,
    const psx_hir_symbol_spec_t *symbol,
    const node_t *source, size_t storage_size);
static int canonical_type_exists(
    const hir_materializer_t *builder, psx_qual_type_t type);
static psx_qual_type_t resolved_expression_type(
    const psx_resolved_hir_node_t *node);
static psx_resolved_hir_node_t *materialize_cast_expression(
    hir_materializer_t *builder, const node_t *source,
    psx_resolved_hir_node_t *operand, psx_qual_type_t qual_type);
static psx_resolved_hir_node_t *materialize_statement(
    hir_materializer_t *builder, const psx_hir_node_spec_t *spec,
    const hir_children_t *children, const node_t *source);

static psx_resolved_hir_node_t *materialize_vla_runtime(
    hir_materializer_t *builder,
    const node_vla_alloc_t *allocation) {
  const node_t *source = allocation
                             ? &allocation->base : NULL;
  const psx_vla_runtime_plan_t *plan =
      allocation ? allocation->runtime_plan : NULL;
  if (!plan || plan->dimension_count <= 0 ||
      !plan->dimensions || plan->element_size <= 0 ||
      (plan->performs_allocation &&
       plan->descriptor_frame_offset <= 0) ||
      (!plan->performs_allocation &&
       plan->descriptor_frame_offset != 0) ||
      plan->stride_store_count < 0 ||
      (plan->stride_store_count > 0 &&
       (!plan->stride_store_offsets ||
        !plan->stride_start_dimensions))) {
    set_failure(
        builder, PSX_RESOLVED_HIR_BUILD_RAW_SYNTAX_REMAINS, source);
    return NULL;
  }

  hir_children_t children = {0};
  for (int i = 0; i < plan->dimension_count; i++) {
    if (!plan->dimensions[i] ||
        !append_child(
            builder, &children,
            build_node(builder, plan->dimensions[i]),
            PSX_HIR_EDGE_VLA_DIMENSION, plan->dimensions[i])) {
      free(children.items);
      free(children.edges);
      return NULL;
    }
  }
  for (int i = 0; i < plan->stride_store_count; i++) {
    if (plan->stride_store_offsets[i] <= 0 ||
        plan->stride_start_dimensions[i] < 0 ||
        plan->stride_start_dimensions[i] >=
            plan->dimension_count) {
      set_failure(
          builder, PSX_RESOLVED_HIR_BUILD_RAW_SYNTAX_REMAINS, source);
      free(children.items);
      free(children.edges);
      return NULL;
    }
  }

  psx_hir_node_spec_t spec = {
      .kind = PSX_HIR_VLA_ALLOC,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
      .storage_offset = plan->descriptor_frame_offset,
      .vla_stride_frame_offset =
          plan->row_stride_frame_offset,
      .vla_stride_element_size = plan->element_size,
      .vla_stride_slot_size = PSX_VLA_RUNTIME_SLOT_SIZE,
      .vla_runtime_store_offsets =
          plan->stride_store_offsets,
      .vla_runtime_store_dimensions =
          plan->stride_start_dimensions,
      .vla_runtime_store_count =
          (size_t)plan->stride_store_count,
  };
  psx_resolved_hir_node_t *result = materialize_statement(
      builder, &spec, &children, source);
  free(children.items);
  free(children.edges);
  return result;
}

static psx_resolved_hir_node_t *materialize_expression_spec(
    hir_materializer_t *builder, const psx_hir_node_spec_t *spec,
    psx_qual_type_t qual_type, const hir_children_t *children,
    const psx_hir_symbol_spec_t *symbol, const node_t *source) {
  psx_resolved_hir_expression_t *expression =
      (psx_resolved_hir_expression_t *)materialize_node_record(
          builder, spec, children, symbol, source,
          sizeof(*expression));
  if (!expression) return NULL;
  expression->qual_type = qual_type;
  return &expression->node;
}

static psx_resolved_hir_node_t *materialize_expression(
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

static psx_resolved_hir_node_t *materialize_statement(
    hir_materializer_t *builder, const psx_hir_node_spec_t *spec,
    const hir_children_t *children, const node_t *source) {
  psx_resolved_hir_statement_t *statement =
      (psx_resolved_hir_statement_t *)materialize_node_record(
          builder, spec, children, NULL, source,
          sizeof(*statement));
  return statement ? &statement->node : NULL;
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
      builder, *prefix, index, resolved_expression_type(index), source);
  return *prefix != NULL;
}

static psx_resolved_hir_node_t *materialize_sizeof_value(
    hir_materializer_t *builder, const node_sizeof_query_t *query,
    psx_qual_type_t qual_type) {
  const psx_sizeof_runtime_plan_t *plan =
      psx_sizeof_query_runtime_plan_const(query);
  if (plan) {
    psx_hir_node_spec_t number_spec = {
        .kind = PSX_HIR_NUMBER,
        .attached_qual_type = {
            PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
        .integer_value = plan->constant_factor,
    };
    psx_resolved_hir_node_t *value = materialize_expression_spec(
        builder, &number_spec, qual_type, NULL, NULL, &query->base);
    if (!value) return NULL;
    for (int i = 0; i < plan->runtime_bound_count; i++) {
      node_t *bound_source = plan->runtime_bounds[i];
      psx_resolved_hir_node_t *bound =
          build_node(builder, bound_source);
      if (!bound) return NULL;
      bound = materialize_cast_expression(
          builder, bound_source, bound, qual_type);
      if (!bound) return NULL;
      psx_resolved_hir_node_t *items[] = {value, bound};
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
      psx_sizeof_query_runtime_size_slot(query);
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
      .integer_value = psx_sizeof_query_resolved_size(query),
  };
  return materialize_expression_spec(
      builder, &spec, qual_type, NULL, NULL, &query->base);
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
  if (!psx_sizeof_query_evaluates_vla_operand(query)) return value;
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
    const hir_children_t *children,
    const psx_hir_symbol_spec_t *symbol,
    const node_t *source, size_t storage_size) {
  psx_resolved_hir_node_t *node = arena_alloc_in(
      builder->arena_context, storage_size);
  if (!node) {
    set_failure(builder, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY, source);
    return NULL;
  }
  node->spec = *spec;
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

static psx_qual_type_t resolved_expression_type(
    const psx_resolved_hir_node_t *node) {
  if (!node || !psx_hir_kind_is_expression(node->spec.kind)) {
    return (psx_qual_type_t){
        PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  }
  return ((const psx_resolved_hir_expression_t *)node)->qual_type;
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
  psx_resolved_hir_node_t *result = materialize_statement(
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
  return materialize_expression_spec(
      builder, &spec, qual_type, NULL, NULL, source);
}

static psx_resolved_hir_node_t *materialize_local_subobject_reference(
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
  return materialize_expression_spec(
      builder, &spec, ps_gvar_decl_qual_type(global),
      NULL, &symbol, source);
}

static psx_resolved_hir_node_t *materialize_runtime_initializer_value(
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

static psx_resolved_hir_node_t *materialize_runtime_initializer_item(
    hir_materializer_t *builder,
    const psx_runtime_initializer_item_t *item,
    const node_t *source) {
  if (!item) return NULL;
  psx_resolved_hir_node_t *value =
      materialize_runtime_initializer_value(
          builder, &item->value, source);
  if (!value ||
      item->kind == PSX_RUNTIME_INITIALIZER_EVALUATE)
    return value;
  psx_resolved_hir_node_t *target =
      materialize_local_subobject_reference(
          builder, item->target.local, item->target.relative_offset,
          item->target.qual_type, item->target.bit_width,
          item->target.bit_offset, item->target.bit_is_signed, source);
  if (!target) return NULL;
  psx_resolved_hir_node_t *items[] = {target, value};
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

static psx_resolved_hir_node_t *materialize_runtime_initializer(
    hir_materializer_t *builder,
    const psx_runtime_initializer_plan_t *plan,
    const node_t *source) {
  if (!plan || plan->item_count <= 0) return NULL;
  psx_resolved_hir_node_t *sequence = NULL;
  for (int i = 0; i < plan->item_count; i++) {
    const psx_runtime_initializer_item_t *item = &plan->items[i];
    psx_resolved_hir_node_t *current =
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
      psx_compound_literal_direct_initializer_const(compound);
  if (direct) return build_node(builder, direct);

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
  if (!resolution->runtime_initializer) return value;
  psx_resolved_hir_node_t *initialization =
      materialize_runtime_initializer(
          builder, resolution->runtime_initializer, source);
  if (!initialization) return NULL;
  return materialize_comma(
      builder, initialization, value, result_type, source);
}

static int source_cast_is_aggregate(
    const node_source_cast_t *cast) {
  psx_source_cast_resolution_kind_t kind =
      psx_source_cast_resolution_kind(cast);
  return kind == PSX_SOURCE_CAST_AGGREGATE_DIRECT_HIR ||
         kind == PSX_SOURCE_CAST_AGGREGATE_TEMPORARY;
}

static int address_requires_typed_hir_lowering(
    const node_t *source) {
  return source && source->kind == ND_ADDR && source->lhs &&
         source->lhs->kind == ND_CAST &&
         source->lhs->is_source_cast &&
         source_cast_is_aggregate(
             (const node_source_cast_t *)source->lhs);
}

static psx_resolved_hir_node_t *materialize_cast_expression(
    hir_materializer_t *builder, const node_t *source,
    psx_resolved_hir_node_t *operand, psx_qual_type_t qual_type) {
  psx_resolved_hir_node_t *items[] = {operand};
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
    psx_resolved_hir_node_t **initialization,
    psx_resolved_hir_node_t **object) {
  const node_t *source = &cast->base;
  const psx_source_cast_resolution_t *resolution =
      psx_source_cast_resolution_state(cast);
  if (!resolution ||
      resolution->kind != PSX_SOURCE_CAST_AGGREGATE_TEMPORARY ||
      !resolution->aggregate_temporary ||
      !initialization || !object)
    return 0;
  psx_resolved_hir_node_t *target =
      materialize_local_subobject_reference(
          builder, resolution->aggregate_temporary,
          resolution->aggregate_member_offset,
          resolution->aggregate_member_qual_type,
          resolution->aggregate_member_bit_width,
          resolution->aggregate_member_bit_offset,
          resolution->aggregate_member_bit_is_signed,
          source);
  psx_resolved_hir_node_t *value =
      build_node(builder, source->lhs);
  *object = materialize_local_object_reference(
      builder, resolution->aggregate_temporary, source);
  if (!target || !value || !*object) return 0;
  psx_resolved_hir_node_t *items[] = {target, value};
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

static psx_resolved_hir_node_t *materialize_aggregate_cast_address(
    hir_materializer_t *builder, const node_source_cast_t *cast,
    psx_qual_type_t result_type, const node_t *address_source) {
  psx_source_cast_resolution_kind_t kind =
      psx_source_cast_resolution_kind(cast);
  if (kind == PSX_SOURCE_CAST_AGGREGATE_DIRECT_HIR) {
    if (cast->base.lhs &&
        cast->base.lhs->kind == ND_CAST &&
        cast->base.lhs->is_source_cast &&
        source_cast_is_aggregate(
            (const node_source_cast_t *)cast->base.lhs)) {
      return materialize_aggregate_cast_address(
          builder, (const node_source_cast_t *)cast->base.lhs,
          result_type, address_source);
    }
    psx_resolved_hir_node_t *object =
        build_node(builder, cast->base.lhs);
    return object
               ? materialize_address_of_object(
                     builder, object, result_type, address_source)
               : NULL;
  }
  if (kind == PSX_SOURCE_CAST_AGGREGATE_TEMPORARY) {
    psx_resolved_hir_node_t *initialization = NULL;
    psx_resolved_hir_node_t *object = NULL;
    if (!materialize_aggregate_temporary_parts(
            builder, cast, &initialization, &object))
      return NULL;
    psx_resolved_hir_node_t *address =
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

static psx_resolved_hir_node_t *materialize_address_expression(
    hir_materializer_t *builder, const node_t *source) {
  psx_qual_type_t result_type = ps_node_qual_type(source);
  if (!canonical_type_exists(builder, result_type)) {
    set_failure(
        builder, PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE, source);
    return NULL;
  }
  return materialize_aggregate_cast_address(
      builder, (const node_source_cast_t *)source->lhs,
      result_type, source);
}

static psx_resolved_hir_node_t *materialize_source_cast(
    hir_materializer_t *builder, const node_source_cast_t *cast) {
  const node_t *source = &cast->base;
  psx_qual_type_t qual_type = ps_node_qual_type(source);
  if (!canonical_type_exists(builder, qual_type)) {
    set_failure(
        builder, PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE, source);
    return NULL;
  }
  psx_source_cast_resolution_kind_t resolution_kind =
      psx_source_cast_resolution_kind(cast);
  if (!source->lhs ||
      (resolution_kind != PSX_SOURCE_CAST_DIRECT_HIR &&
       resolution_kind != PSX_SOURCE_CAST_AGGREGATE_DIRECT_HIR &&
       resolution_kind != PSX_SOURCE_CAST_AGGREGATE_TEMPORARY)) {
    set_failure(
        builder, PSX_RESOLVED_HIR_BUILD_RAW_SYNTAX_REMAINS, source);
    return NULL;
  }
  if (resolution_kind == PSX_SOURCE_CAST_AGGREGATE_TEMPORARY) {
    psx_resolved_hir_node_t *initialization = NULL;
    psx_resolved_hir_node_t *object = NULL;
    if (!materialize_aggregate_temporary_parts(
            builder, cast, &initialization, &object))
      return NULL;
    return materialize_comma(
        builder, initialization, object, qual_type, source);
  }
  psx_resolved_hir_node_t *operand =
      build_node(builder, source->lhs);
  if (!operand) return NULL;
  return materialize_cast_expression(
      builder, source, operand, qual_type);
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
          psx_alignof_query_resolved_alignment(
              (const node_alignof_query_t *)source);
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
      const psx_member_access_state_t *state =
          psx_member_access_state(access);
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
      if (!case_node->has_resolved_value || source->lhs) {
        set_failure(
            builder, PSX_RESOLVED_HIR_BUILD_RAW_SYNTAX_REMAINS, source);
        return 0;
      }
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
  if (source && source->kind == ND_VLA_ALLOC) {
    return materialize_vla_runtime(
        builder, (const node_vla_alloc_t *)source);
  }
  if (address_requires_typed_hir_lowering(source))
    return materialize_address_expression(builder, source);
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
    const node_t *selected =
        psx_generic_selection_selected_expression_const(selection);
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
  if (source && source->kind == ND_CAST && source->is_source_cast) {
    return materialize_source_cast(
        builder, (const node_source_cast_t *)source);
  }
  psx_hir_node_spec_t spec = {0};
  psx_qual_type_t qual_type = {
      PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  spec.attached_qual_type = qual_type;
  if (!map_kind(source->kind, &spec.kind)) {
    set_failure(builder, PSX_RESOLVED_HIR_BUILD_RAW_SYNTAX_REMAINS, source);
    return NULL;
  }
  if (source->kind == ND_ASSIGN &&
      source->is_source_compound_assignment)
    spec.kind = PSX_HIR_COMPOUND_ASSIGN;
  int is_expression = psx_hir_kind_is_expression(spec.kind);
  if (is_expression) {
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
  if (is_expression &&
      !derive_structural_expression_type(
          builder, source, &children, &qual_type)) {
    free(children.items);
    free(children.edges);
    return NULL;
  }
  psx_resolved_hir_node_t *result =
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

int psx_resolution_work_tree_build_typed_hir(
    psx_resolution_work_tree_t *work_tree,
    const psx_semantic_context_t *semantic_context,
    psx_resolved_hir_build_failure_t *failure) {
  if (failure) memset(failure, 0, sizeof(*failure));
  const node_t *semantic_root =
      psx_resolution_work_tree_semantic_root(work_tree);
  if (!work_tree || !semantic_context || !semantic_root) {
    if (failure) {
      failure->status = PSX_RESOLVED_HIR_BUILD_INVALID_INPUT;
      failure->source_node_kind = semantic_root
                                      ? (int)semantic_root->kind : -1;
    }
    return 0;
  }
  psx_resolution_work_phase_t phase =
      psx_resolution_work_tree_phase(work_tree);
  if (phase != PSX_RESOLUTION_WORK_FINALIZED) {
    if (failure) {
      failure->status = PSX_RESOLVED_HIR_BUILD_UNFINALIZED_RESOLUTION;
      failure->source_node_kind = (int)semantic_root->kind;
    }
    return 0;
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
    return 0;
  }
  psx_typed_hir_tree_t *typed_tree = arena_alloc_in(
      builder.arena_context, sizeof(*typed_tree));
  if (!typed_tree) {
    if (failure && failure->status == PSX_RESOLVED_HIR_BUILD_OK)
      failure->status = PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY;
    return 0;
  }
  typed_tree->root = root;
  if (!psx_resolution_work_tree_attach_typed_hir(
          work_tree, typed_tree)) {
    if (failure && failure->status == PSX_RESOLVED_HIR_BUILD_OK)
      failure->status = PSX_RESOLVED_HIR_BUILD_INVALID_INPUT;
    return 0;
  }
  return 1;
}
