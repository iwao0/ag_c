#include "identifier_binding.h"

#include "identifier_resolution.h"
#include "alignof_query_resolution.h"
#include "sizeof_query_resolution.h"
#include "vla_runtime_plan.h"
#include "../parser/arena.h"
#include "../parser/declaration_syntax.h"
#include "../parser/diag.h"
#include "../parser/global_registry.h"
#include "../parser/lvar_internal.h"
#include "../parser/node_utils.h"
#include "../parser/semantic_ctx.h"
#include "../parser/symtab.h"
#include "../parser/type_builder.h"

#include <string.h>

typedef struct {
  psx_semantic_context_t *semantic_context;
  psx_global_registry_t *global_registry;
  psx_local_registry_t *local_registry;
  const token_t *fallback_diag_tok;
  psx_local_lookup_point_t lookup_point;
  int has_lookup_point_override;
} psx_identifier_binding_context_t;

static node_t *bind_node(
    node_t *node, const psx_identifier_binding_context_t *context);

static void bind_slot(
    node_t **slot, const psx_identifier_binding_context_t *context) {
  if (slot && *slot) *slot = bind_node(*slot, context);
}

static void copy_identifier_source_state(
    node_t *target, const node_identifier_t *identifier) {
  if (!target || !identifier) return;
  target->tok = identifier->base.tok;
  target->usage_region = identifier->base.usage_region;
  target->lvar_usage_unevaluated =
      identifier->base.lvar_usage_unevaluated;
}

static int is_static_local_array(const lvar_t *var) {
  return var && var->is_static_local && var->static_global_name &&
         ps_lvar_is_array(var) && !ps_lvar_is_vla(var) && !var->is_param;
}

static node_t *materialize_local(
    arena_context_t *arena_context,
    const node_identifier_t *identifier, lvar_t *var) {
  node_t *node = NULL;
  if (is_static_local_array(var)) {
    node = psx_node_new_static_local_array_addr_for_in(
        arena_context, var);
  } else if (ps_lvar_is_array(var) && !ps_lvar_is_vla(var)) {
    node = ps_node_new_lvar_array_addr_for_in(arena_context, var);
  } else if (ps_lvar_is_vla(var)) {
    node = psx_node_new_vla_decay_ref_for_in(arena_context, var);
  } else {
    node = psx_node_new_lvar_identifier_ref_for_in(arena_context, var);
  }
  copy_identifier_source_state(node, identifier);
  node->usage_lvar = var;
  node->records_lvar_usage = 1;
  return node;
}

static node_t *materialize_global(
    arena_context_t *arena_context,
    const node_identifier_t *identifier, global_var_t *global) {
  node_t *node = ps_gvar_is_array(global)
      ? ps_node_new_gvar_array_addr_for_in(arena_context, global)
      : ps_node_new_gvar_for_in(arena_context, global);
  copy_identifier_source_state(node, identifier);
  return node;
}

static node_t *materialize_function(
    const node_identifier_t *identifier,
    const psx_identifier_resolution_t *resolution,
    const psx_identifier_binding_context_t *context) {
  const psx_type_t *function_type =
      ps_function_symbol_type(resolution->function);
  node_funcref_t *reference = arena_alloc_in(
      ps_ctx_arena(context->semantic_context), sizeof(*reference));
  if (!ps_node_prepare_resolution_state_in(
          ps_ctx_arena(context->semantic_context),
          (node_t *)reference))
    return NULL;
  reference->base.kind = ND_FUNCREF;
  ps_node_bind_type(
      (node_t *)reference,
      function_type
          ? ps_type_clone_in(
                ps_ctx_arena(context->semantic_context), function_type)
          : NULL);
  reference->funcname = identifier->name;
  reference->funcname_len = identifier->name_len;
  copy_identifier_source_state((node_t *)reference, identifier);
  return (node_t *)reference;
}

static node_t *materialize_builtin_va_arg_area(
    const node_identifier_t *identifier,
    const psx_identifier_binding_context_t *context) {
  node_t *node = arena_alloc_in(
      ps_ctx_arena(context->semantic_context), sizeof(*node));
  if (!node ||
      !ps_node_prepare_resolution_state_in(
          ps_ctx_arena(context->semantic_context), node))
    return NULL;
  node->kind = ND_VA_ARG_AREA;
  copy_identifier_source_state(node, identifier);
  return node;
}

static void resolve_identifier(
    const node_identifier_t *identifier, int is_call,
    const psx_identifier_binding_context_t *context,
    psx_identifier_resolution_t *resolution) {
  psx_local_lookup_point_t point = context->has_lookup_point_override
      ? context->lookup_point
      : (psx_local_lookup_point_t){
            .scope_seq = identifier->scope_seq,
            .declaration_seq = identifier->declaration_seq,
        };
  psx_resolve_identifier(
      &(psx_identifier_resolution_request_t){
          .semantic_context = context->semantic_context,
          .global_registry = context->global_registry,
          .local_registry = context->local_registry,
          .name = identifier->name,
          .name_len = identifier->name_len,
          .is_call = is_call,
          .has_local_lookup_point = 1,
          .local_lookup_point = point,
      },
      resolution);
}

static node_t *materialize_identifier(
    node_identifier_t *identifier, int is_call,
    const psx_identifier_binding_context_t *context,
    psx_identifier_resolution_t *out_resolution) {
  if (!is_call && identifier->name_len == 13 &&
      memcmp(identifier->name, "__va_arg_area", 13) == 0) {
    if (out_resolution)
      *out_resolution = (psx_identifier_resolution_t){0};
    return materialize_builtin_va_arg_area(identifier, context);
  }
  psx_identifier_resolution_t resolution;
  resolve_identifier(identifier, is_call, context, &resolution);
  if (out_resolution) *out_resolution = resolution;
  switch (resolution.kind) {
    case PSX_IDENTIFIER_LOCAL:
      return materialize_local(
          ps_ctx_arena(context->semantic_context),
          identifier, resolution.local);
    case PSX_IDENTIFIER_ENUM_CONSTANT: {
      node_t *node = ps_node_new_num_in(
          ps_ctx_arena(context->semantic_context), resolution.enum_value);
      copy_identifier_source_state(node, identifier);
      return node;
    }
    case PSX_IDENTIFIER_GLOBAL_OBJECT:
      return materialize_global(
          ps_ctx_arena(context->semantic_context),
          identifier, resolution.global);
    case PSX_IDENTIFIER_FUNCTION:
      return materialize_function(identifier, &resolution, context);
    case PSX_IDENTIFIER_UNDECLARED_CALL:
      return NULL;
    case PSX_IDENTIFIER_UNDEFINED:
      psx_diag_undefined_with_name_in(
          ps_ctx_diagnostics(context->semantic_context),
          identifier->base.tok
              ? identifier->base.tok
              : (token_t *)context->fallback_diag_tok,
          "variable", identifier->name, identifier->name_len);
      return NULL;
  }
  return NULL;
}

static node_t *materialize_address_operand(
    node_identifier_t *identifier,
    const psx_identifier_binding_context_t *context) {
  psx_identifier_resolution_t resolution;
  resolve_identifier(identifier, 0, context, &resolution);
  if (resolution.kind == PSX_IDENTIFIER_LOCAL && resolution.local) {
    lvar_t *var = resolution.local;
    node_t *node = NULL;
    if (is_static_local_array(var)) {
      node = psx_node_new_static_local_gvar_for_in(
          ps_ctx_arena(context->semantic_context), var);
    } else if (ps_lvar_is_array(var)) {
      node = psx_node_new_lvar_object_ref_for_in(
          ps_ctx_arena(context->semantic_context), var);
    }
    if (node) {
      copy_identifier_source_state(node, identifier);
      node->usage_lvar = var;
      node->records_lvar_usage = 1;
      return node;
    }
  }
  if (resolution.kind == PSX_IDENTIFIER_GLOBAL_OBJECT &&
      resolution.global && ps_gvar_is_array(resolution.global)) {
    node_t *node = psx_node_new_gvar_array_base_for_in(
        ps_ctx_arena(context->semantic_context), resolution.global);
    copy_identifier_source_state(node, identifier);
    return node;
  }
  if (resolution.kind == PSX_IDENTIFIER_FUNCTION)
    return materialize_function(identifier, &resolution, context);
  return materialize_identifier(
      identifier, 0, context, NULL);
}

static void bind_type_name(
    psx_type_name_ref_t *type_name,
    const psx_identifier_binding_context_t *context) {
  if (!type_name || !type_name->syntax) return;
  psx_parsed_declarator_t *declarator =
      &type_name->syntax->declarator;
  for (int i = 0; i < declarator->array_bound_count; i++)
    bind_slot(
        &declarator->array_bounds[i].expression.node,
        context);
  if (type_name->syntax->atomic_inner)
    bind_type_name(
        &(psx_type_name_ref_t){
            .syntax = type_name->syntax->atomic_inner,
        },
        context);
}

static node_t *bind_initializer(
    node_t *syntax, const psx_identifier_binding_context_t *context) {
  if (!syntax) return NULL;
  if (syntax->kind != ND_INIT_LIST)
    return bind_node(syntax, context);
  node_init_list_t *list = (node_init_list_t *)syntax;
  for (int i = 0; i < list->entry_count; i++) {
    psx_initializer_entry_t *entry = &list->entries[i];
    for (int d = 0; d < entry->designator_count; d++) {
      psx_initializer_designator_t *designator =
          &entry->designators[d];
      bind_slot(&designator->index_expr, context);
      bind_slot(&designator->range_end_expr, context);
    }
    for (int d = 0; d < entry->index_expr_count; d++)
      bind_slot(&entry->index_exprs[d], context);
    entry->value = bind_initializer(entry->value, context);
  }
  return syntax;
}

static void bind_direct_call(
    node_function_call_t *call, node_identifier_t *identifier,
    const psx_identifier_binding_context_t *context) {
  psx_identifier_resolution_t resolution;
  node_t *callee = materialize_identifier(
      identifier, 1, context, &resolution);
  if (resolution.kind != PSX_IDENTIFIER_FUNCTION &&
      resolution.kind != PSX_IDENTIFIER_UNDECLARED_CALL) {
    call->callee = callee;
    return;
  }

  call->callee = NULL;
  call->direct_name = identifier->name;
  call->direct_name_len = identifier->name_len;
  call->base.tok = identifier->base.tok;
  if (resolution.kind == PSX_IDENTIFIER_UNDECLARED_CALL) {
    call->base.is_implicit_func_decl = 1;
    return;
  }
  const psx_type_t *function_type =
      ps_function_symbol_type(resolution.function);
  call->callee_type = function_type
      ? ps_type_clone_in(
            ps_ctx_arena(context->semantic_context), function_type)
      : NULL;
  if (!call->callee_type ||
      call->callee_type->kind != PSX_TYPE_FUNCTION) {
    ps_diag_ctx_in(
        ps_ctx_diagnostics(context->semantic_context),
        identifier->base.tok
            ? identifier->base.tok
            : (token_t *)context->fallback_diag_tok,
        "funcall", "canonical function type is missing for '%.*s'",
        identifier->name_len, identifier->name);
  }
  int expected = call->callee_type->param_count;
  int is_variadic = call->callee_type->is_variadic_function;
  int mismatch = is_variadic
      ? call->argument_count < expected
      : call->argument_count != expected;
  if (mismatch) {
    ps_diag_ctx_in(
        ps_ctx_diagnostics(context->semantic_context),
        identifier->base.tok
            ? identifier->base.tok
            : (token_t *)context->fallback_diag_tok,
        "funcall",
        "関数呼び出しの引数数が一致しません: '%.*s' 期待 %s%d、実際 %d",
        identifier->name_len, identifier->name,
        is_variadic ? ">=" : "", expected, call->argument_count);
  }
}

static node_t *bind_node(
    node_t *node, const psx_identifier_binding_context_t *context) {
  if (!node) return NULL;
  switch (node->kind) {
    case ND_IDENTIFIER:
      return materialize_identifier(
          (node_identifier_t *)node, 0, context, NULL);
    case ND_BLOCK: {
      node_t **body = ((node_block_t *)node)->body;
      for (int i = 0; body && body[i]; i++)
        bind_slot(&body[i], context);
      return node;
    }
    case ND_FUNCDEF: {
      node_function_definition_t *function =
          (node_function_definition_t *)node;
      for (int i = 0; i < function->parameter_count; i++)
        bind_slot(&function->parameters[i], context);
      bind_slot(&node->rhs, context);
      return node;
    }
    case ND_FUNCALL: {
      node_function_call_t *call = (node_function_call_t *)node;
      for (int i = 0; i < call->argument_count; i++)
        bind_slot(&call->arguments[i], context);
      if (call->callee && call->callee->kind == ND_IDENTIFIER)
        bind_direct_call(
            call, (node_identifier_t *)call->callee,
            context);
      else
        bind_slot(&call->callee, context);
      return node;
    }
    case ND_ADDR:
      if (node->is_explicit_addr_expr && node->lhs &&
          node->lhs->kind == ND_IDENTIFIER) {
        node->lhs = materialize_address_operand(
            (node_identifier_t *)node->lhs,
            context);
        if (node->lhs && node->lhs->kind == ND_FUNCREF)
          return node->lhs;
        return node;
      }
      bind_slot(&node->lhs, context);
      return node;
    case ND_DECL_INIT: {
      node_decl_init_t *init = (node_decl_init_t *)node;
      bind_slot(&node->lhs, context);
      if (init->init_kind == PSX_DECL_INIT_LIST)
        node->rhs = bind_initializer(node->rhs, context);
      else
        bind_slot(&node->rhs, context);
      return node;
    }
    case ND_INIT_LIST:
      return bind_initializer(node, context);
    case ND_STATIC_ASSERT: {
      node_static_assert_t *assertion =
          (node_static_assert_t *)node;
      bind_slot(&assertion->condition, context);
      return node;
    }
    case ND_VLA_ALLOC: {
      psx_vla_runtime_plan_t *plan =
          ((node_vla_alloc_t *)node)->runtime_plan;
      for (int i = 0; plan && i < plan->dimension_count; i++)
        bind_slot(&plan->dimensions[i], context);
      return node;
    }
    case ND_COMPOUND_LITERAL: {
      node_compound_literal_t *literal =
          (node_compound_literal_t *)node;
      bind_type_name(&literal->type_name, context);
      node->rhs = bind_initializer(node->rhs, context);
      return node;
    }
    case ND_CAST:
      if (node->is_source_cast)
        bind_type_name(
            &((node_source_cast_t *)node)->type_name,
            context);
      bind_slot(&node->lhs, context);
      return node;
    case ND_GENERIC_SELECTION: {
      node_generic_selection_t *selection =
          (node_generic_selection_t *)node;
      bind_slot(&selection->control, context);
      for (int i = 0; i < selection->association_count; i++) {
        if (!selection->associations[i].is_default)
          bind_type_name(
              &selection->associations[i].type_name,
              context);
        bind_slot(
            &selection->associations[i].expression,
            context);
      }
      return node;
    }
    case ND_SIZEOF_QUERY: {
      node_sizeof_query_t *query = (node_sizeof_query_t *)node;
      if (psx_sizeof_query_resolved_size(query) > 0 ||
          psx_sizeof_query_runtime_size_slot(query) != 0 ||
          psx_sizeof_query_runtime_plan(query) ||
          (query->is_type_name && query->type_name.resolved_type))
        return node;
      bind_type_name(&query->type_name, context);
      bind_slot(&query->operand, context);
      return node;
    }
    case ND_ALIGNOF_QUERY:
      if (psx_alignof_query_resolved_alignment(
              (node_alignof_query_t *)node) > 0)
        return node;
      bind_type_name(
          &((node_alignof_query_t *)node)->type_name,
          context);
      return node;
    case ND_IF:
    case ND_FOR:
    case ND_TERNARY: {
      node_ctrl_t *control = (node_ctrl_t *)node;
      bind_slot(&control->init, context);
      bind_slot(&node->lhs, context);
      bind_slot(&node->rhs, context);
      bind_slot(&control->inc, context);
      bind_slot(&control->els, context);
      return node;
    }
    default:
      bind_slot(&node->lhs, context);
      bind_slot(&node->rhs, context);
      return node;
  }
}

node_t *psx_bind_identifier_tree_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    node_t *node, const token_t *fallback_diag_tok) {
  if (!semantic_context || !global_registry || !local_registry) return node;
  const psx_identifier_binding_context_t context = {
      .semantic_context = semantic_context,
      .global_registry = global_registry,
      .local_registry = local_registry,
      .fallback_diag_tok = fallback_diag_tok,
  };
  return bind_node(node, &context);
}

node_t *psx_bind_identifier_tree_at_lookup_point_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_local_lookup_point_t lookup_point,
    node_t *node, const token_t *fallback_diag_tok) {
  if (!semantic_context || !global_registry || !local_registry) return node;
  const psx_identifier_binding_context_t context = {
      .semantic_context = semantic_context,
      .global_registry = global_registry,
      .local_registry = local_registry,
      .fallback_diag_tok = fallback_diag_tok,
      .lookup_point = lookup_point,
      .has_lookup_point_override = 1,
  };
  return bind_node(node, &context);
}

node_t *psx_bind_identifier_tree_in_session(
    ag_compilation_session_t *session,
    node_t *node, const token_t *fallback_diag_tok) {
  if (!ag_compilation_session_is_complete(session)) return node;
  return psx_bind_identifier_tree_in_contexts(
      ag_compilation_session_semantic_context(session),
      ag_compilation_session_global_registry(session),
      ag_compilation_session_local_registry(session),
      node, fallback_diag_tok);
}

node_t *psx_bind_identifier_initializer_tree_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    node_t *syntax, const token_t *fallback_diag_tok) {
  if (!semantic_context || !global_registry || !local_registry) return syntax;
  const psx_identifier_binding_context_t context = {
      .semantic_context = semantic_context,
      .global_registry = global_registry,
      .local_registry = local_registry,
      .fallback_diag_tok = fallback_diag_tok,
  };
  return bind_initializer(syntax, &context);
}

node_t *psx_bind_identifier_initializer_tree_in_session(
    ag_compilation_session_t *session,
    node_t *syntax, const token_t *fallback_diag_tok) {
  if (!ag_compilation_session_is_complete(session)) return syntax;
  return psx_bind_identifier_initializer_tree_in_contexts(
      ag_compilation_session_semantic_context(session),
      ag_compilation_session_global_registry(session),
      ag_compilation_session_local_registry(session),
      syntax, fallback_diag_tok);
}
