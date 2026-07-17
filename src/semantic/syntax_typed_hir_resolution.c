#include "syntax_typed_hir_resolution.h"

#include <string.h>

#include "../parser/arena.h"
#include "../parser/ast.h"
#include "../parser/semantic_ctx.h"
#include "expression_operand_resolution.h"
#include "hir_symbol_resolution.h"
#include "identifier_resolution.h"
#include "literal_resolution.h"
#include "semantic_node_builder.h"
#include "typed_hir_tree_internal.h"

typedef struct {
  psx_semantic_context_t *semantic_context;
  psx_global_registry_t *global_registry;
  psx_local_registry_t *local_registry;
  psx_semantic_node_builder_t builder;
  psx_resolved_hir_build_failure_t *failure;
} direct_resolution_context_t;

static void set_failure(
    psx_resolved_hir_build_failure_t *failure,
    psx_resolved_hir_build_status_t status,
    const node_t *source) {
  if (!failure) return;
  failure->status = status;
  failure->source_node_kind = source ? source->kind : -1;
}

static psx_typed_hir_tree_t *wrap_typed_root(
    psx_semantic_context_t *semantic_context,
    psx_semantic_node_t *root,
    const node_t *source,
    psx_resolved_hir_build_failure_t *failure) {
  if (!semantic_context || !root) return NULL;
  psx_typed_hir_tree_t *tree = arena_alloc_in(
      ps_ctx_arena(semantic_context), sizeof(*tree));
  if (!tree) {
    set_failure(
        failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY, source);
    return NULL;
  }
  tree->root = root;
  return tree;
}

static int resolve_direct_identifier(
    const direct_resolution_context_t *context,
    const node_identifier_t *identifier,
    psx_identifier_expression_resolution_t *resolution) {
  if (!context || !identifier) return 0;
  psx_identifier_expression_resolution_t resolved;
  psx_resolve_identifier_expression(
      &(psx_identifier_resolution_request_t){
          .semantic_context = context->semantic_context,
          .global_registry = context->global_registry,
          .local_registry = context->local_registry,
          .name = identifier->name,
          .name_len = identifier->name_len,
          .has_local_lookup_point = 1,
          .local_lookup_point = {
              .scope_seq = identifier->scope_seq,
              .declaration_seq = identifier->declaration_seq,
          },
      },
      &resolved);
  switch (resolved.symbol.kind) {
    case PSX_IDENTIFIER_ENUM_CONSTANT:
    case PSX_IDENTIFIER_GLOBAL_OBJECT:
    case PSX_IDENTIFIER_FUNCTION:
      break;
    case PSX_IDENTIFIER_LOCAL:
    case PSX_IDENTIFIER_UNDECLARED_CALL:
    case PSX_IDENTIFIER_UNDEFINED:
      return 0;
  }
  if (resolved.expression_qual_type.type_id == PSX_TYPE_ID_INVALID)
    return 0;
  if (resolution) *resolution = resolved;
  return 1;
}

static int direct_binary_kind(
    psx_syntax_node_kind_t syntax_kind,
    psx_hir_node_kind_t *hir_kind) {
#define MAP(kind, hir) case kind: *hir_kind = hir; return 1
  if (!hir_kind) return 0;
  switch (syntax_kind) {
    MAP(ND_ADD, PSX_HIR_ADD);
    MAP(ND_SUB, PSX_HIR_SUB);
    MAP(ND_MUL, PSX_HIR_MUL);
    MAP(ND_DIV, PSX_HIR_DIV);
    MAP(ND_MOD, PSX_HIR_MOD);
    MAP(ND_BITAND, PSX_HIR_BITAND);
    MAP(ND_BITXOR, PSX_HIR_BITXOR);
    MAP(ND_BITOR, PSX_HIR_BITOR);
    MAP(ND_SHL, PSX_HIR_SHL);
    MAP(ND_SHR, PSX_HIR_SHR);
    MAP(ND_EQ, PSX_HIR_EQ);
    MAP(ND_NE, PSX_HIR_NE);
    MAP(ND_LT, PSX_HIR_LT);
    MAP(ND_LE, PSX_HIR_LE);
    MAP(ND_LOGAND, PSX_HIR_LOGAND);
    MAP(ND_LOGOR, PSX_HIR_LOGOR);
    default:
      return 0;
  }
#undef MAP
}

static int preflight_direct_expression(
    const direct_resolution_context_t *context,
    const node_t *syntax,
    psx_qual_type_t *qual_type) {
  if (qual_type)
    *qual_type = (psx_qual_type_t){
        PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  if (!syntax) return 0;
  if (syntax->kind == ND_NUM) {
    psx_literal_semantic_resolution_t resolution;
    if (!psx_resolve_number_literal_semantics_in_contexts(
            context->semantic_context, NULL,
            (const node_num_t *)syntax, &resolution))
      return 0;
    if (qual_type) *qual_type = resolution.qual_type;
    return 1;
  }
  if (syntax->kind == ND_IDENTIFIER) {
    psx_identifier_expression_resolution_t resolution;
    if (!resolve_direct_identifier(
            context, (const node_identifier_t *)syntax,
            &resolution))
      return 0;
    if (qual_type) *qual_type = resolution.expression_qual_type;
    return 1;
  }
  if (syntax->kind == ND_UNARY_NEGATE) {
    psx_qual_type_t operand_type;
    if (!preflight_direct_expression(
            context, syntax->lhs, &operand_type))
      return 0;
    psx_qual_type_t result =
        psx_resolve_arithmetic_unary_result_qual_type_in(
            context->semantic_context, ND_UNARY_NEGATE,
            operand_type);
    if (result.type_id == PSX_TYPE_ID_INVALID) return 0;
    if (qual_type) *qual_type = result;
    return 1;
  }
  if (syntax->kind == ND_TERNARY) {
    const node_ctrl_t *ternary = (const node_ctrl_t *)syntax;
    psx_qual_type_t condition_type;
    psx_qual_type_t then_type;
    psx_qual_type_t else_type;
    if (!preflight_direct_expression(
            context, syntax->lhs, &condition_type) ||
        !psx_qual_type_is_scalar_in(
            context->semantic_context, condition_type) ||
        !preflight_direct_expression(
            context, syntax->rhs, &then_type) ||
        !preflight_direct_expression(
            context, ternary->els, &else_type))
      return 0;
    psx_qual_type_t result =
        psx_resolve_conditional_result_qual_type_in(
            context->semantic_context, then_type, else_type);
    if (result.type_id == PSX_TYPE_ID_INVALID) return 0;
    if (qual_type) *qual_type = result;
    return 1;
  }
  psx_hir_node_kind_t hir_kind;
  psx_qual_type_t lhs_type;
  psx_qual_type_t rhs_type;
  if (!direct_binary_kind(syntax->kind, &hir_kind) ||
      !preflight_direct_expression(
          context, syntax->lhs, &lhs_type) ||
      !preflight_direct_expression(
          context, syntax->rhs, &rhs_type))
    return 0;
  psx_qual_type_t result = psx_resolve_binary_result_qual_type_in(
      context->semantic_context, syntax->kind,
      lhs_type, rhs_type);
  if (result.type_id == PSX_TYPE_ID_INVALID) return 0;
  if (qual_type) *qual_type = result;
  return 1;
}

static psx_semantic_node_t *build_direct_literal(
    direct_resolution_context_t *context,
    const node_t *syntax) {
  if (!context || !syntax) return NULL;
  const node_t *literal_syntax = syntax;

  psx_literal_semantic_resolution_t resolution;
  int resolved = literal_syntax->kind == ND_NUM
      ? psx_resolve_number_literal_semantics_in_contexts(
            context->semantic_context, context->global_registry,
            (const node_num_t *)literal_syntax, &resolution)
      : psx_resolve_string_literal_semantics_in_contexts(
            context->semantic_context, context->global_registry,
            (const node_string_t *)literal_syntax, &resolution);
  if (!resolved) {
    set_failure(
        context->failure,
        PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE, syntax);
    return NULL;
  }

  psx_hir_node_spec_t spec = {
      .kind = literal_syntax->kind == ND_NUM
                  ? PSX_HIR_NUMBER : PSX_HIR_STRING,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
  };
  if (literal_syntax->kind == ND_NUM) {
    const node_num_t *number =
        (const node_num_t *)literal_syntax;
    spec.integer_value = number->val;
    spec.floating_value = number->fval;
  } else {
    const node_string_t *string =
        (const node_string_t *)literal_syntax;
    spec.name = resolution.string_label;
    spec.name_length = resolution.string_label
                           ? strlen(resolution.string_label) : 0;
    spec.literal_contents = string->literal_contents;
    spec.literal_length = string->literal_length > 0
                              ? (size_t)string->literal_length : 0;
    int character_width = (int)string->char_width;
    if (character_width <= 0) character_width = 1;
    spec.object_size = (string->byte_len + 1) * character_width;
    spec.object_align = character_width;
  }
  return psx_semantic_node_builder_leaf_expression(
      &context->builder, &spec, resolution.qual_type, NULL,
      syntax->kind);
}

static psx_semantic_node_t *build_direct_identifier(
    direct_resolution_context_t *context,
    const node_identifier_t *identifier) {
  psx_identifier_expression_resolution_t resolution;
  if (!resolve_direct_identifier(context, identifier, &resolution))
    return NULL;
  if (resolution.symbol.kind == PSX_IDENTIFIER_ENUM_CONSTANT) {
    node_num_t literal = {0};
    literal.base.kind = ND_NUM;
    literal.base.tok = identifier->base.tok;
    literal.val = resolution.symbol.enum_value;
    return build_direct_literal(context, &literal.base);
  }

  psx_hir_node_spec_t spec = {
      .kind = resolution.symbol.kind == PSX_IDENTIFIER_FUNCTION
                  ? PSX_HIR_FUNCTION_REF : PSX_HIR_GLOBAL,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
      .name = identifier->name,
      .name_length = identifier->name_len > 0
                         ? (size_t)identifier->name_len : 0,
  };
  if (resolution.symbol.kind == PSX_IDENTIFIER_FUNCTION)
    return psx_semantic_node_builder_leaf_expression(
        &context->builder, &spec,
        resolution.expression_qual_type, NULL,
        identifier->base.kind);

  psx_hir_symbol_spec_t symbol;
  if (!psx_resolve_global_hir_symbol_spec_in(
          context->semantic_context,
          resolution.symbol.global, &symbol)) {
    set_failure(
        context->failure,
        PSX_RESOLVED_HIR_BUILD_MISSING_RESOLVED_SYMBOL,
        &identifier->base);
    return NULL;
  }
  spec.name = symbol.name;
  spec.name_length = symbol.name_length;
  psx_semantic_node_t *object =
      psx_semantic_node_builder_leaf_expression(
          &context->builder, &spec,
          resolution.declaration_qual_type, &symbol,
          identifier->base.kind);
  if (!object || !resolution.decays_array_to_address)
    return object;

  psx_semantic_node_t *children[] = {object};
  psx_hir_edge_kind_t edges[] = {PSX_HIR_EDGE_LHS};
  psx_hir_node_spec_t address_spec = {
      .kind = PSX_HIR_ADDRESS,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
  };
  return psx_semantic_node_builder_expression(
      &context->builder, &address_spec,
      resolution.expression_qual_type,
      children, edges, 1, NULL,
      identifier->base.kind);
}

static psx_semantic_node_t *build_direct_expression(
    direct_resolution_context_t *context,
    const node_t *syntax) {
  if (syntax->kind == ND_NUM)
    return build_direct_literal(context, syntax);
  if (syntax->kind == ND_IDENTIFIER)
    return build_direct_identifier(
        context, (const node_identifier_t *)syntax);

  if (syntax->kind == ND_UNARY_NEGATE) {
    psx_semantic_node_t *operand =
        build_direct_expression(context, syntax->lhs);
    if (!operand) return NULL;
    psx_qual_type_t result_qual_type =
        psx_resolve_arithmetic_unary_result_qual_type_in(
            context->semantic_context, ND_UNARY_NEGATE,
            psx_semantic_node_expression_qual_type(operand));
    if (result_qual_type.type_id == PSX_TYPE_ID_INVALID) {
      set_failure(
          context->failure,
          PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE, syntax);
      return NULL;
    }
    psx_semantic_node_t *children[] = {operand};
    psx_hir_edge_kind_t edges[] = {PSX_HIR_EDGE_LHS};
    psx_hir_node_spec_t spec = {
        .kind = PSX_HIR_NEGATE,
        .attached_qual_type = {
            PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
    };
    return psx_semantic_node_builder_expression(
        &context->builder, &spec, result_qual_type,
        children, edges, 1, NULL, syntax->kind);
  }

  if (syntax->kind == ND_TERNARY) {
    const node_ctrl_t *ternary = (const node_ctrl_t *)syntax;
    psx_semantic_node_t *condition =
        build_direct_expression(context, syntax->lhs);
    psx_semantic_node_t *then_value =
        build_direct_expression(context, syntax->rhs);
    psx_semantic_node_t *else_value =
        build_direct_expression(context, ternary->els);
    if (!condition || !then_value || !else_value) return NULL;
    psx_qual_type_t result_qual_type =
        psx_resolve_conditional_result_qual_type_in(
            context->semantic_context,
            psx_semantic_node_expression_qual_type(then_value),
            psx_semantic_node_expression_qual_type(else_value));
    if (result_qual_type.type_id == PSX_TYPE_ID_INVALID) {
      set_failure(
          context->failure,
          PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE, syntax);
      return NULL;
    }
    psx_semantic_node_t *children[] = {
        condition, then_value, else_value};
    psx_hir_edge_kind_t edges[] = {
        PSX_HIR_EDGE_LHS, PSX_HIR_EDGE_RHS, PSX_HIR_EDGE_ELSE};
    psx_hir_node_spec_t spec = {
        .kind = PSX_HIR_TERNARY,
        .attached_qual_type = {
            PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
    };
    return psx_semantic_node_builder_expression(
        &context->builder, &spec, result_qual_type,
        children, edges, 3, NULL, syntax->kind);
  }

  psx_hir_node_kind_t hir_kind;
  if (!direct_binary_kind(syntax->kind, &hir_kind))
    return NULL;
  psx_semantic_node_t *lhs =
      build_direct_expression(context, syntax->lhs);
  psx_semantic_node_t *rhs =
      build_direct_expression(context, syntax->rhs);
  if (!lhs || !rhs) return NULL;

  psx_qual_type_t lhs_qual_type =
      psx_semantic_node_expression_qual_type(lhs);
  psx_qual_type_t rhs_qual_type =
      psx_semantic_node_expression_qual_type(rhs);
  psx_qual_type_t result_qual_type =
      psx_resolve_binary_result_qual_type_in(
          context->semantic_context, syntax->kind,
          lhs_qual_type, rhs_qual_type);
  if (result_qual_type.type_id == PSX_TYPE_ID_INVALID) {
    set_failure(
        context->failure,
        PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE, syntax);
    return NULL;
  }

  psx_semantic_node_t *children[] = {lhs, rhs};
  psx_hir_edge_kind_t edges[] = {
      PSX_HIR_EDGE_LHS, PSX_HIR_EDGE_RHS};
  psx_hir_node_spec_t spec = {
      .kind = hir_kind,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
  };
  return psx_semantic_node_builder_expression(
      &context->builder, &spec, result_qual_type,
      children, edges, 2, NULL, syntax->kind);
}

psx_syntax_typed_hir_resolution_status_t
psx_resolve_syntax_expression_direct_to_typed_hir_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const node_t *syntax_expression,
    const psx_typed_hir_tree_t **typed_hir,
    psx_resolved_hir_build_failure_t *failure) {
  if (typed_hir) *typed_hir = NULL;
  if (failure) memset(failure, 0, sizeof(*failure));
  if (!semantic_context || !global_registry || !local_registry ||
      !syntax_expression || !typed_hir) {
    set_failure(
        failure, PSX_RESOLVED_HIR_BUILD_INVALID_INPUT,
        syntax_expression);
    return PSX_SYNTAX_TYPED_HIR_FAILED;
  }

  direct_resolution_context_t context = {
      .semantic_context = semantic_context,
      .global_registry = global_registry,
      .local_registry = local_registry,
      .failure = failure,
  };
  psx_semantic_node_builder_init(
      &context.builder, ps_ctx_arena(semantic_context),
      semantic_context, failure);

  psx_semantic_node_t *root = NULL;
  if (syntax_expression->kind == ND_STRING) {
    root = build_direct_literal(&context, syntax_expression);
  } else {
    psx_qual_type_t preflight_type;
    if (!preflight_direct_expression(
            &context, syntax_expression, &preflight_type))
      return PSX_SYNTAX_TYPED_HIR_NOT_HANDLED;
    root = build_direct_expression(&context, syntax_expression);
  }

  psx_typed_hir_tree_t *tree = wrap_typed_root(
      semantic_context, root, syntax_expression, failure);
  if (!tree) return PSX_SYNTAX_TYPED_HIR_FAILED;
  *typed_hir = tree;
  return PSX_SYNTAX_TYPED_HIR_RESOLVED;
}
