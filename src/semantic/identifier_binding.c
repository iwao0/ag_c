#include "identifier_binding.h"

#include "identifier_resolution.h"
#include "../parser/arena.h"
#include "../parser/declaration_syntax.h"
#include "../parser/diag.h"
#include "../parser/lvar_internal.h"
#include "../parser/node_utils.h"
#include "../parser/symtab.h"
#include "../parser/type_builder.h"

#include <string.h>

static node_t *bind_node(node_t *node, const token_t *fallback_diag_tok);

static void bind_slot(node_t **slot, const token_t *fallback_diag_tok) {
  if (slot && *slot) *slot = bind_node(*slot, fallback_diag_tok);
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
    const node_identifier_t *identifier, lvar_t *var) {
  node_t *node = NULL;
  if (is_static_local_array(var)) {
    node = psx_node_new_static_local_array_addr_for(var);
  } else if (ps_lvar_is_array(var) && !ps_lvar_is_vla(var)) {
    node = ps_node_new_lvar_array_addr_for(var);
  } else if (ps_lvar_is_vla(var)) {
    node = psx_node_new_vla_decay_ref_for(var);
  } else {
    node = psx_node_new_lvar_identifier_ref_for(var);
  }
  copy_identifier_source_state(node, identifier);
  node->usage_lvar = var;
  node->records_lvar_usage = 1;
  return node;
}

static node_t *materialize_global(
    const node_identifier_t *identifier, global_var_t *global) {
  node_t *node = ps_gvar_is_array(global)
      ? ps_node_new_gvar_array_addr_for(global)
      : ps_node_new_gvar_for(global);
  copy_identifier_source_state(node, identifier);
  return node;
}

static node_t *materialize_function(
    const node_identifier_t *identifier,
    const psx_identifier_resolution_t *resolution) {
  const psx_type_t *function_type =
      ps_function_symbol_type(resolution->function);
  node_funcref_t *reference = arena_alloc(sizeof(*reference));
  reference->base.kind = ND_FUNCREF;
  ps_node_bind_type(
      (node_t *)reference,
      function_type
          ? ps_type_clone(function_type)
          : NULL);
  reference->funcname = identifier->name;
  reference->funcname_len = identifier->name_len;
  copy_identifier_source_state((node_t *)reference, identifier);
  return (node_t *)reference;
}

static void resolve_identifier(
    const node_identifier_t *identifier, int is_call,
    psx_identifier_resolution_t *resolution) {
  psx_resolve_identifier(
      &(psx_identifier_resolution_request_t){
          .name = identifier->name,
          .name_len = identifier->name_len,
          .is_call = is_call,
          .has_local_lookup_point = 1,
          .local_lookup_point = {
              .scope_seq = identifier->scope_seq,
              .declaration_seq = identifier->declaration_seq,
          },
      },
      resolution);
}

static node_t *materialize_identifier(
    node_identifier_t *identifier, int is_call,
    const token_t *fallback_diag_tok,
    psx_identifier_resolution_t *out_resolution) {
  psx_identifier_resolution_t resolution;
  resolve_identifier(identifier, is_call, &resolution);
  if (out_resolution) *out_resolution = resolution;
  switch (resolution.kind) {
    case PSX_IDENTIFIER_LOCAL:
      return materialize_local(identifier, resolution.local);
    case PSX_IDENTIFIER_ENUM_CONSTANT: {
      node_t *node = ps_node_new_num(resolution.enum_value);
      copy_identifier_source_state(node, identifier);
      return node;
    }
    case PSX_IDENTIFIER_GLOBAL_OBJECT:
      return materialize_global(identifier, resolution.global);
    case PSX_IDENTIFIER_FUNCTION:
      return materialize_function(identifier, &resolution);
    case PSX_IDENTIFIER_UNDECLARED_CALL:
      return NULL;
    case PSX_IDENTIFIER_UNDEFINED:
      psx_diag_undefined_with_name(
          identifier->base.tok
              ? identifier->base.tok
              : (token_t *)fallback_diag_tok,
          "variable", identifier->name, identifier->name_len);
      return NULL;
  }
  return NULL;
}

static node_t *materialize_address_operand(
    node_identifier_t *identifier,
    const token_t *fallback_diag_tok) {
  psx_identifier_resolution_t resolution;
  resolve_identifier(identifier, 0, &resolution);
  if (resolution.kind == PSX_IDENTIFIER_LOCAL && resolution.local) {
    lvar_t *var = resolution.local;
    node_t *node = NULL;
    if (is_static_local_array(var)) {
      node = psx_node_new_static_local_gvar_for(var);
    } else if (ps_lvar_is_array(var)) {
      node = psx_node_new_lvar_object_ref_for(var);
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
    node_t *node = psx_node_new_gvar_array_base_for(
        resolution.global);
    copy_identifier_source_state(node, identifier);
    return node;
  }
  if (resolution.kind == PSX_IDENTIFIER_FUNCTION)
    return materialize_function(identifier, &resolution);
  return materialize_identifier(
      identifier, 0, fallback_diag_tok, NULL);
}

static void bind_type_name(
    psx_type_name_ref_t *type_name, const token_t *fallback_diag_tok) {
  if (!type_name || !type_name->syntax) return;
  psx_parsed_declarator_t *declarator =
      &type_name->syntax->declarator;
  for (int i = 0; i < declarator->array_bound_count; i++)
    bind_slot(
        &declarator->array_bounds[i].expression.node,
        fallback_diag_tok);
  if (type_name->syntax->atomic_inner)
    bind_type_name(
        &(psx_type_name_ref_t){
            .syntax = type_name->syntax->atomic_inner,
        },
        fallback_diag_tok);
}

static node_t *bind_initializer(
    node_t *syntax, const token_t *fallback_diag_tok) {
  if (!syntax) return NULL;
  if (syntax->kind != ND_INIT_LIST)
    return bind_node(syntax, fallback_diag_tok);
  node_init_list_t *list = (node_init_list_t *)syntax;
  for (int i = 0; i < list->entry_count; i++) {
    psx_initializer_entry_t *entry = &list->entries[i];
    for (int d = 0; d < entry->designator_count; d++) {
      psx_initializer_designator_t *designator =
          &entry->designators[d];
      bind_slot(&designator->index_expr, fallback_diag_tok);
      bind_slot(&designator->range_end_expr, fallback_diag_tok);
    }
    for (int d = 0; d < entry->index_expr_count; d++)
      bind_slot(&entry->index_exprs[d], fallback_diag_tok);
    entry->value = bind_initializer(entry->value, fallback_diag_tok);
  }
  return syntax;
}

static void bind_direct_call(
    node_func_t *call, node_identifier_t *identifier,
    const token_t *fallback_diag_tok) {
  psx_identifier_resolution_t resolution;
  node_t *callee = materialize_identifier(
      identifier, 1, fallback_diag_tok, &resolution);
  if (resolution.kind != PSX_IDENTIFIER_FUNCTION &&
      resolution.kind != PSX_IDENTIFIER_UNDECLARED_CALL) {
    call->callee = callee;
    return;
  }

  call->callee = NULL;
  call->funcname = identifier->name;
  call->funcname_len = identifier->name_len;
  call->base.tok = identifier->base.tok;
  if (resolution.kind == PSX_IDENTIFIER_UNDECLARED_CALL) {
    call->base.is_implicit_func_decl = 1;
    return;
  }
  const psx_type_t *function_type =
      ps_function_symbol_type(resolution.function);
  call->function_type = function_type
      ? ps_type_clone(function_type)
      : NULL;
  if (!call->function_type ||
      call->function_type->kind != PSX_TYPE_FUNCTION) {
    ps_diag_ctx(
        identifier->base.tok
            ? identifier->base.tok
            : (token_t *)fallback_diag_tok,
        "funcall", "canonical function type is missing for '%.*s'",
        identifier->name_len, identifier->name);
  }
  int expected = call->function_type->param_count;
  int is_variadic = call->function_type->is_variadic_function;
  int mismatch = is_variadic
      ? call->nargs < expected
      : call->nargs != expected;
  if (mismatch) {
    ps_diag_ctx(
        identifier->base.tok
            ? identifier->base.tok
            : (token_t *)fallback_diag_tok,
        "funcall",
        "関数呼び出しの引数数が一致しません: '%.*s' 期待 %s%d、実際 %d",
        identifier->name_len, identifier->name,
        is_variadic ? ">=" : "", expected, call->nargs);
  }
}

static node_t *bind_node(node_t *node, const token_t *fallback_diag_tok) {
  if (!node) return NULL;
  switch (node->kind) {
    case ND_IDENTIFIER:
      return materialize_identifier(
          (node_identifier_t *)node, 0, fallback_diag_tok, NULL);
    case ND_BLOCK: {
      node_t **body = ((node_block_t *)node)->body;
      for (int i = 0; body && body[i]; i++)
        bind_slot(&body[i], fallback_diag_tok);
      return node;
    }
    case ND_FUNCDEF: {
      node_func_t *function = (node_func_t *)node;
      for (int i = 0; i < function->nargs; i++)
        bind_slot(&function->args[i], fallback_diag_tok);
      bind_slot(&node->rhs, fallback_diag_tok);
      return node;
    }
    case ND_FUNCALL: {
      node_func_t *call = (node_func_t *)node;
      for (int i = 0; i < call->nargs; i++)
        bind_slot(&call->args[i], fallback_diag_tok);
      if (call->callee && call->callee->kind == ND_IDENTIFIER)
        bind_direct_call(
            call, (node_identifier_t *)call->callee,
            fallback_diag_tok);
      else
        bind_slot(&call->callee, fallback_diag_tok);
      return node;
    }
    case ND_ADDR:
      if (node->is_explicit_addr_expr && node->lhs &&
          node->lhs->kind == ND_IDENTIFIER) {
        node->lhs = materialize_address_operand(
            (node_identifier_t *)node->lhs,
            fallback_diag_tok);
        if (node->lhs && node->lhs->kind == ND_FUNCREF)
          return node->lhs;
        return node;
      }
      bind_slot(&node->lhs, fallback_diag_tok);
      return node;
    case ND_DECL_INIT: {
      node_decl_init_t *init = (node_decl_init_t *)node;
      bind_slot(&node->lhs, fallback_diag_tok);
      if (init->init_kind == PSX_DECL_INIT_LIST)
        node->rhs = bind_initializer(node->rhs, fallback_diag_tok);
      else
        bind_slot(&node->rhs, fallback_diag_tok);
      return node;
    }
    case ND_INIT_LIST:
      return bind_initializer(node, fallback_diag_tok);
    case ND_COMPOUND_LITERAL: {
      node_compound_literal_t *literal =
          (node_compound_literal_t *)node;
      bind_type_name(&literal->type_name, fallback_diag_tok);
      node->rhs = bind_initializer(node->rhs, fallback_diag_tok);
      return node;
    }
    case ND_CAST:
      if (node->is_source_cast)
        bind_type_name(
            &((node_source_cast_t *)node)->type_name,
            fallback_diag_tok);
      bind_slot(&node->lhs, fallback_diag_tok);
      return node;
    case ND_GENERIC_SELECTION: {
      node_generic_selection_t *selection =
          (node_generic_selection_t *)node;
      if (selection->selected_index >= 0) return node;
      bind_slot(&selection->control, fallback_diag_tok);
      for (int i = 0; i < selection->association_count; i++) {
        if (!selection->associations[i].is_default)
          bind_type_name(
              &selection->associations[i].type_name,
              fallback_diag_tok);
        bind_slot(
            &selection->associations[i].expression,
            fallback_diag_tok);
      }
      return node;
    }
    case ND_SIZEOF_QUERY: {
      node_sizeof_query_t *query = (node_sizeof_query_t *)node;
      if (query->queried_type || query->resolved_size > 0 ||
          query->runtime_size_slot != 0)
        return node;
      bind_type_name(&query->type_name, fallback_diag_tok);
      bind_slot(&query->operand, fallback_diag_tok);
      bind_slot(&query->runtime_size_expr, fallback_diag_tok);
      return node;
    }
    case ND_ALIGNOF_QUERY:
      if (((node_alignof_query_t *)node)->resolved_alignment > 0)
        return node;
      bind_type_name(
          &((node_alignof_query_t *)node)->type_name,
          fallback_diag_tok);
      return node;
    case ND_IF:
    case ND_FOR:
    case ND_TERNARY: {
      node_ctrl_t *control = (node_ctrl_t *)node;
      bind_slot(&control->init, fallback_diag_tok);
      bind_slot(&node->lhs, fallback_diag_tok);
      bind_slot(&node->rhs, fallback_diag_tok);
      bind_slot(&control->inc, fallback_diag_tok);
      bind_slot(&control->els, fallback_diag_tok);
      return node;
    }
    default:
      bind_slot(&node->lhs, fallback_diag_tok);
      bind_slot(&node->rhs, fallback_diag_tok);
      return node;
  }
}

node_t *psx_bind_identifier_tree(
    node_t *node, const token_t *fallback_diag_tok) {
  return bind_node(node, fallback_diag_tok);
}

node_t *psx_bind_identifier_initializer_tree(
    node_t *syntax, const token_t *fallback_diag_tok) {
  return bind_initializer(syntax, fallback_diag_tok);
}
