#include "identifier_binding.h"

#include "identifier_resolution.h"
#include "alignof_query_resolution.h"
#include "function_call_resolution.h"
#include "sizeof_query_resolution.h"
#include "type_name_resolution.h"
#include "vla_runtime_plan.h"
#include "resolved_function.h"
#include "resolved_node.h"
#include "resolved_node_kind.h"
#include "resolved_node_type.h"
#include "resolved_object_ref.h"
#include "../parser/arena.h"
#include "../parser/declaration_syntax.h"
#include "../parser/decl.h"
#include "../parser/diag.h"
#include "../parser/global_registry.h"
#include "../parser/lvar_internal.h"
#include "../parser/node_utils.h"
#include "../parser/semantic_ctx.h"
#include "../parser/symtab.h"
#include "../diag/diag.h"

#include <string.h>

typedef struct {
  psx_semantic_context_t *semantic_context;
  psx_global_registry_t *global_registry;
  psx_local_registry_t *local_registry;
  const token_t *fallback_diag_tok;
  psx_local_lookup_point_t lookup_point;
  int has_lookup_point_override;
  int usage_is_unevaluated;
  char *function_name;
  int function_name_len;
} psx_identifier_binding_context_t;

static psx_resolution_store_t *binding_store(
    const psx_identifier_binding_context_t *context) {
  return context
             ? ps_ctx_resolution_store(context->semantic_context)
             : NULL;
}

static node_t *bind_node(
    node_t *node, const psx_identifier_binding_context_t *context);

static void bind_slot(
    node_t **slot, const psx_identifier_binding_context_t *context) {
  if (slot && *slot) *slot = bind_node(*slot, context);
}

static int is_static_local_array(const lvar_t *var) {
  return var && var->is_static_local && var->static_global_name &&
         ps_lvar_is_array(var) && !ps_lvar_is_vla(var) && !var->is_param;
}

static void record_lvar_usage(
    const psx_identifier_binding_context_t *context,
    node_t *node, lvar_t *var) {
  psx_resolution_store_t *store = binding_store(context);
  ps_node_record_lvar_usage(store, node, var);
  ps_node_set_lvar_usage_unevaluated(
      store, node, context && context->usage_is_unevaluated);
}

static node_t *wrap_array_decay(
    psx_resolution_store_t *store,
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types,
    node_t *reference, psx_qual_type_t expression_qual_type) {
  const psx_type_t *expression_type =
      psx_semantic_type_table_lookup_qual_type(
          semantic_types, expression_qual_type);
  if (!expression_type) return NULL;
  node_t *address = psx_resolution_node_alloc_in(
      store, arena_context, sizeof(*address));
  if (!address ||
      !psx_resolution_node_set_kind(store, address, ND_ADDR))
    return NULL;
  address->lhs = reference;
  ps_node_bind_qual_type(
      store, address, expression_type, expression_qual_type);
  return address;
}

static int bind_static_local_reference(
    psx_resolution_store_t *store,
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types,
    node_t *node, lvar_t *var, psx_qual_type_t declaration_qual_type) {
  global_var_t *global = var ? var->static_global : NULL;
  const psx_type_t *type = psx_semantic_type_table_lookup_qual_type(
      semantic_types, declaration_qual_type);
  return psx_bind_global_reference_in(
      store, arena_context, node, global,
      var ? var->static_global_name : NULL,
      var ? var->static_global_name_len : 0,
      type, declaration_qual_type,
      global && global->is_thread_local);
}

static node_t *materialize_local(
    const psx_identifier_binding_context_t *context,
    node_identifier_t *identifier,
    const psx_identifier_expression_resolution_t *resolution) {
  lvar_t *var = resolution ? resolution->symbol.local : NULL;
  psx_resolution_store_t *store = binding_store(context);
  arena_context_t *arena_context =
      ps_ctx_arena(context->semantic_context);
  const psx_semantic_type_table_t *semantic_types =
      ps_ctx_semantic_type_table_in(context->semantic_context);
  node_t *node = (node_t *)identifier;
  if (is_static_local_array(var)) {
    if (!bind_static_local_reference(
            store, arena_context, semantic_types, node, var,
            resolution->declaration_qual_type))
      return NULL;
    node = wrap_array_decay(
        store, arena_context, semantic_types, node,
        resolution->expression_qual_type);
  } else if (ps_lvar_is_array(var) && !ps_lvar_is_vla(var)) {
    if (!psx_bind_local_reference_in(
            store, arena_context, node, var, var->offset,
            semantic_types, resolution->declaration_qual_type))
      return NULL;
    node = wrap_array_decay(
        store, arena_context, semantic_types, node,
        resolution->expression_qual_type);
  } else if (ps_lvar_is_vla(var)) {
    if (!psx_bind_local_reference_in(
            store, arena_context, node, var, var->offset,
            semantic_types, resolution->expression_qual_type))
      return NULL;
  } else if (var && var->is_static_local && var->static_global_name) {
    if (!bind_static_local_reference(
            store, arena_context, semantic_types, node, var,
            resolution->declaration_qual_type))
      return NULL;
  } else {
    if (!psx_bind_local_reference_in(
            store, arena_context, node, var, var ? var->offset : 0,
            semantic_types, resolution->expression_qual_type))
      return NULL;
  }
  record_lvar_usage(context, node, var);
  return node;
}

static node_t *materialize_global(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types,
    node_identifier_t *identifier,
    const psx_identifier_expression_resolution_t *resolution) {
  global_var_t *global = resolution ? resolution->symbol.global : NULL;
  const psx_type_t *declaration_type =
      psx_semantic_type_table_lookup_qual_type(
          semantic_types, resolution->declaration_qual_type);
  node_t *node = (node_t *)identifier;
  if (!psx_bind_global_reference_in(
          store, arena_context, node, global,
          global ? global->name : identifier->name,
          global ? global->name_len : identifier->name_len,
          declaration_type, resolution->declaration_qual_type,
          global && global->is_thread_local))
    return NULL;
  return global && ps_gvar_is_array(global)
             ? wrap_array_decay(
                   store, arena_context, semantic_types, node,
                   resolution->expression_qual_type)
             : node;
}

static node_t *materialize_function(
    node_identifier_t *identifier,
    const psx_identifier_resolution_t *resolution,
    const psx_identifier_binding_context_t *context) {
  psx_qual_type_t function_qual_type =
      ps_function_symbol_qual_type(resolution->function);
  return psx_bind_function_reference_in(
             binding_store(context),
             ps_ctx_arena(context->semantic_context),
             (node_t *)identifier, identifier->name,
             identifier->name_len,
             ps_ctx_semantic_type_table_in(
                 context->semantic_context),
             function_qual_type)
             ? (node_t *)identifier : NULL;
}

static node_t *materialize_builtin_va_arg_area(
    node_identifier_t *identifier,
    const psx_identifier_binding_context_t *context) {
  return psx_bind_va_arg_area_reference_in(
             binding_store(context),
             ps_ctx_arena(context->semantic_context),
             (node_t *)identifier)
             ? (node_t *)identifier : NULL;
}

static psx_identifier_resolution_request_t identifier_resolution_request(
    const node_identifier_t *identifier, int is_call,
    const psx_identifier_binding_context_t *context) {
  psx_local_lookup_point_t point = context->has_lookup_point_override
      ? context->lookup_point
      : (psx_local_lookup_point_t){
            .scope_seq = identifier->scope_seq,
            .declaration_seq = identifier->declaration_seq,
        };
  int has_lookup_point = context->has_lookup_point_override ||
                         identifier->base.tok ||
                         identifier->scope_seq != 0 ||
                         identifier->declaration_seq != 0;
  return (psx_identifier_resolution_request_t){
      .semantic_context = context->semantic_context,
      .global_registry = context->global_registry,
      .local_registry = context->local_registry,
      .name = identifier->name,
      .name_len = identifier->name_len,
      .is_call = is_call,
      .has_local_lookup_point = has_lookup_point,
      .local_lookup_point = point,
  };
}

static void resolve_identifier(
    const node_identifier_t *identifier, int is_call,
    const psx_identifier_binding_context_t *context,
    psx_identifier_resolution_t *resolution) {
  psx_identifier_resolution_request_t request =
      identifier_resolution_request(identifier, is_call, context);
  psx_resolve_identifier(&request, resolution);
}

static void resolve_identifier_expression(
    const node_identifier_t *identifier, int is_call,
    const psx_identifier_binding_context_t *context,
    psx_identifier_expression_resolution_t *resolution) {
  psx_identifier_resolution_request_t request =
      identifier_resolution_request(identifier, is_call, context);
  psx_resolve_identifier_expression(&request, resolution);
}

static int is_predefined_function_name(
    const psx_identifier_binding_context_t *context,
    const node_identifier_t *identifier) {
  static const char name[] = "__func__";
  return context && context->function_name && identifier &&
         identifier->name_len == (int)(sizeof(name) - 1) &&
         memcmp(identifier->name, name, sizeof(name) - 1) == 0;
}

static node_t *materialize_predefined_function_name(
    const psx_identifier_binding_context_t *context,
    const node_identifier_t *identifier) {
  if (!is_predefined_function_name(context, identifier)) return NULL;
  node_string_t *string = psx_resolution_node_alloc_in(
      binding_store(context), ps_ctx_arena(context->semantic_context),
      sizeof(*string));
  if (!string) return NULL;
  string->base.kind = ND_STRING;
  string->base.tok = identifier->base.tok;
  string->literal_contents = context->function_name;
  string->literal_length = context->function_name_len;
  string->char_width = TK_CHAR_WIDTH_CHAR;
  string->str_prefix_kind = TK_STR_PREFIX_NONE;
  string->byte_len = context->function_name_len;
  return &string->base;
}

static node_t *materialize_identifier(
    node_identifier_t *identifier, int is_call,
    const psx_identifier_binding_context_t *context,
    psx_identifier_resolution_t *out_resolution) {
  if (is_predefined_function_name(context, identifier)) {
    if (out_resolution) *out_resolution = (psx_identifier_resolution_t){0};
    return materialize_predefined_function_name(context, identifier);
  }
  psx_identifier_expression_resolution_t resolution;
  resolve_identifier_expression(
      identifier, is_call, context, &resolution);
  if (out_resolution) *out_resolution = resolution.symbol;
  switch (resolution.symbol.kind) {
    case PSX_IDENTIFIER_LOCAL:
      return materialize_local(context, identifier, &resolution);
    case PSX_IDENTIFIER_ENUM_CONSTANT: {
      node_t *node = ps_node_new_num_in(
          binding_store(context),
          ps_ctx_arena(context->semantic_context),
          resolution.symbol.enum_value);
      node->tok = identifier->base.tok;
      ps_node_set_lvar_usage_region(
          binding_store(context), node,
          ps_node_lvar_usage_region(
              binding_store(context), &identifier->base));
      return node;
    }
    case PSX_IDENTIFIER_GLOBAL_OBJECT:
      return materialize_global(
          binding_store(context),
          ps_ctx_arena(context->semantic_context),
          ps_ctx_semantic_type_table_in(context->semantic_context),
          identifier, &resolution);
    case PSX_IDENTIFIER_FUNCTION:
      return materialize_function(
          identifier, &resolution.symbol, context);
    case PSX_IDENTIFIER_BUILTIN_VA_ARG_AREA:
      return materialize_builtin_va_arg_area(identifier, context);
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
  if (is_predefined_function_name(context, identifier))
    return materialize_predefined_function_name(context, identifier);
  psx_identifier_resolution_t resolution;
  resolve_identifier(identifier, 0, context, &resolution);
  const psx_semantic_type_table_t *semantic_types =
      ps_ctx_semantic_type_table_in(context->semantic_context);
  if (resolution.kind == PSX_IDENTIFIER_LOCAL && resolution.local) {
    lvar_t *var = resolution.local;
    node_t *node = (node_t *)identifier;
    if (is_static_local_array(var)) {
      if (!bind_static_local_reference(
              binding_store(context),
              ps_ctx_arena(context->semantic_context), semantic_types,
              node, var, ps_lvar_decl_qual_type(var)))
        return NULL;
    } else if (ps_lvar_is_array(var)) {
      if (!psx_bind_local_reference_in(
              binding_store(context),
              ps_ctx_arena(context->semantic_context), node, var,
              var->offset, semantic_types,
              ps_lvar_decl_qual_type(var)))
        return NULL;
    } else {
      return materialize_identifier(
          identifier, 0, context, NULL);
    }
    record_lvar_usage(context, node, var);
    return node;
  }
  if (resolution.kind == PSX_IDENTIFIER_GLOBAL_OBJECT &&
      resolution.global && ps_gvar_is_array(resolution.global)) {
    psx_qual_type_t declaration_qual_type =
        ps_gvar_decl_qual_type(resolution.global);
    return psx_bind_global_reference_in(
               binding_store(context),
               ps_ctx_arena(context->semantic_context),
               (node_t *)identifier, resolution.global,
               resolution.global->name, resolution.global->name_len,
               psx_semantic_type_table_lookup_qual_type(
                   semantic_types, declaration_qual_type),
               declaration_qual_type,
               resolution.global->is_thread_local)
               ? (node_t *)identifier : NULL;
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
  psx_function_call_bind_direct_name(
      binding_store(context), call,
      identifier->name, identifier->name_len);
  call->base.tok = identifier->base.tok;
  if (resolution.kind == PSX_IDENTIFIER_UNDECLARED_CALL) {
    psx_function_call_set_implicit_declaration(
        binding_store(context), call, 1);
    return;
  }
  psx_qual_type_t function_qual_type =
      ps_function_symbol_qual_type(resolution.function);
  const psx_semantic_type_table_t *semantic_types =
      ps_ctx_semantic_type_table_in(context->semantic_context);
  const psx_type_t *callee_type =
      psx_semantic_type_table_lookup_qual_type(
          semantic_types, function_qual_type);
  psx_function_call_bind_qual_type(
      binding_store(context), call, semantic_types,
      function_qual_type);
  if (!callee_type || callee_type->kind != PSX_TYPE_FUNCTION) {
    ps_diag_ctx_in(
        ps_ctx_diagnostics(context->semantic_context),
        identifier->base.tok
            ? identifier->base.tok
            : (token_t *)context->fallback_diag_tok,
        "funcall", "canonical function type is missing for '%.*s'",
        identifier->name_len, identifier->name);
    return;
  }
}

static node_t *bind_node(
    node_t *node, const psx_identifier_binding_context_t *context) {
  if (!node) return NULL;
  switch (psx_resolution_node_kind(binding_store(context), node)) {
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
      psx_identifier_binding_context_t function_context = *context;
      function_context.function_name = function->name;
      function_context.function_name_len = function->name_len;
      for (int i = 0; i < function->parameter_count; i++)
        bind_slot(&function->parameters[i], &function_context);
      bind_slot(&node->rhs, &function_context);
      return node;
    }
    case ND_FUNCALL: {
      node_function_call_t *call = (node_function_call_t *)node;
      if (psx_function_call_builtin_kind(call) ==
          PSX_BUILTIN_CALL_EXPECT) {
        const node_t *value =
            psx_builtin_expect_value_operand(call);
        if (!value) {
          ag_diagnostic_context_t *diagnostics =
              ps_ctx_diagnostics(context->semantic_context);
          diag_emit_tokf_in(
              diagnostics,
              DIAG_ERR_PARSER_CALL_ARGUMENT_COUNT_MISMATCH,
              call->base.tok
                  ? call->base.tok
                  : (token_t *)context->fallback_diag_tok,
              "%s", diag_message_for_in(
                        diagnostics,
                        DIAG_ERR_PARSER_CALL_ARGUMENT_COUNT_MISMATCH));
          return NULL;
        }
        return bind_node((node_t *)value, context);
      }
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
    case ND_ADDRESS_OF:
      if (node->lhs && node->lhs->kind == ND_IDENTIFIER) {
        node->lhs = materialize_address_operand(
            (node_identifier_t *)node->lhs,
            context);
        if (node->lhs &&
            psx_resolved_object_ref_node_kind(
                binding_store(context), node->lhs) == ND_FUNCREF)
          return node->lhs;
        return node;
      }
      bind_slot(&node->lhs, context);
      return node;
    case ND_ADDR:
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
    case ND_VLA_ALLOC:
      return node;
    case ND_COMPOUND_LITERAL: {
      node_compound_literal_t *literal =
          (node_compound_literal_t *)node;
      bind_type_name(&literal->type_name, context);
      node->rhs = bind_initializer(node->rhs, context);
      return node;
    }
    case ND_SOURCE_CAST:
      bind_type_name(
          &((node_source_cast_t *)node)->type_name,
          context);
      bind_slot(&node->lhs, context);
      return node;
    case ND_GENERIC_SELECTION: {
      node_generic_selection_t *selection =
          (node_generic_selection_t *)node;
      psx_identifier_binding_context_t unevaluated = *context;
      unevaluated.usage_is_unevaluated = 1;
      bind_slot(&selection->control, &unevaluated);
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
      if (psx_sizeof_query_resolved_size(
              binding_store(context), query) > 0 ||
          psx_sizeof_query_runtime_size_slot(
              binding_store(context), query) != 0 ||
          psx_sizeof_query_runtime_plan(binding_store(context), query) ||
          (query->is_type_name &&
           psx_node_resolved_type_name(
               binding_store(context), &query->base)))
        return node;
      bind_type_name(&query->type_name, context);
      psx_identifier_binding_context_t unevaluated = *context;
      unevaluated.usage_is_unevaluated = 1;
      bind_slot(&query->operand, &unevaluated);
      return node;
    }
    case ND_ALIGNOF_QUERY:
      if (psx_alignof_query_resolved_alignment(
              binding_store(context),
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

static void set_binding_function_name(
    psx_identifier_binding_context_t *context) {
  if (!context || !context->local_registry) return;
  ps_decl_get_current_funcname_in(
      context->local_registry, &context->function_name,
      &context->function_name_len);
}

node_t *psx_bind_identifier_tree_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    node_t *node, const token_t *fallback_diag_tok) {
  if (!semantic_context || !global_registry || !local_registry) return node;
  psx_identifier_binding_context_t context = {
      .semantic_context = semantic_context,
      .global_registry = global_registry,
      .local_registry = local_registry,
      .fallback_diag_tok = fallback_diag_tok,
  };
  set_binding_function_name(&context);
  return bind_node(node, &context);
}

node_t *psx_bind_identifier_tree_at_lookup_point_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_local_lookup_point_t lookup_point,
    node_t *node, const token_t *fallback_diag_tok) {
  if (!semantic_context || !global_registry || !local_registry) return node;
  psx_identifier_binding_context_t context = {
      .semantic_context = semantic_context,
      .global_registry = global_registry,
      .local_registry = local_registry,
      .fallback_diag_tok = fallback_diag_tok,
      .lookup_point = lookup_point,
      .has_lookup_point_override = 1,
  };
  set_binding_function_name(&context);
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
  psx_identifier_binding_context_t context = {
      .semantic_context = semantic_context,
      .global_registry = global_registry,
      .local_registry = local_registry,
      .fallback_diag_tok = fallback_diag_tok,
  };
  set_binding_function_name(&context);
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
