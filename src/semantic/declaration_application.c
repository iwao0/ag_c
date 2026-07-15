#include "declaration_application.h"
#include "declaration_registration.h"
#include "declaration_type_builder.h"

#include "../parser/arena.h"
#include "../parser/diag.h"
#include "../parser/declarator_shape_builder.h"
#include "../parser/enum_const.h"
#include "../parser/decl.h"
#include "../parser/global_registry.h"
#include "../parser/local_registry.h"
#include "../parser/node_utils.h"
#include "../parser/semantic_ctx.h"
#include "../parser/type_builder.h"
#include "aggregate_member_resolution.h"
#include "declaration_resolution.h"
#include "enum_constant_resolution.h"
#include "constant_expression.h"
#include "function_parameter_resolution.h"
#include "identifier_binding.h"
#include "semantic_pass.h"
#include "../diag/diag.h"
#include "../diag/error_catalog.h"

#include <stdlib.h>

int psx_apply_parsed_enum_body_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_parsed_enum_body_t *body) {
  if (!semantic_context || !global_registry || !local_registry || !body)
    return 0;
  long long next_value = 0;
  for (int i = 0; i < body->member_count; i++) {
    const psx_parsed_enum_member_t *member = &body->members[i];
    long long value = next_value;
    if (member->initializer)
      value = psx_resolve_prepared_enum_const_expr_in_context(
          semantic_context, member->initializer);
    psx_apply_parsed_enum_constant_in_contexts(
        semantic_context, global_registry, local_registry,
        member->enumerator->str, member->enumerator->len, value,
        (token_t *)member->enumerator);
    next_value = value + 1;
  }
  return body->member_count;
}

int psx_apply_parsed_aggregate_body_layout_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parsed_aggregate_body_t *body,
    token_kind_t tag_kind, char *tag_name, int tag_len,
    int *out_size, int *out_align) {
  if (!semantic_context || !global_registry || !local_registry ||
      !body || !out_size) return 0;
  int member_count = 0;
  psx_aggregate_layout_state_t layout;
  psx_aggregate_layout_init(&layout, tag_kind);
  for (int i = 0; i < body->item_count; i++) {
    psx_parsed_aggregate_item_t *item = &body->items[i];
    if (item->kind == PSX_PARSED_AGGREGATE_STATIC_ASSERT) {
      psx_apply_static_assert_in_contexts(
          semantic_context, global_registry, local_registry,
          item->value.static_assertion.condition,
          item->value.static_assertion.diagnostic_token);
      continue;
    }

    psx_parsed_aggregate_member_declaration_t *declaration =
        &item->value.member_declaration;
    ps_prepare_decl_specifier_alignments_in_context(
        &declaration->specifier, semantic_context);
    const psx_type_t *member_base_type =
        psx_apply_parsed_decl_specifier_in_contexts(
            semantic_context, global_registry, local_registry,
            &declaration->specifier);
    if (!member_base_type) {
      ps_diag_ctx_in(ps_ctx_diagnostics(semantic_context), declaration->specifier.diagnostic_token, "decl", "%s",
                   diag_message_for_in(ps_ctx_diagnostics(semantic_context),
                       DIAG_ERR_PARSER_MEMBER_TYPE_REQUIRED));
    }
    int requested_alignment =
        psx_apply_parsed_decl_alignment(
            semantic_context, &declaration->specifier);
    for (int j = 0; j < declaration->declarator_count; j++) {
      psx_parsed_declarator_t *head = &declaration->declarators[j];
      ps_prepare_constant_declarator_expressions_in_context(
          head, semantic_context);
      psx_declarator_shape_t resolved_shape;
      int resolved_bit_width = 0;
      psx_apply_parsed_declarator_in_contexts(
          semantic_context, global_registry, local_registry,
          head, &resolved_shape, &resolved_bit_width);
      member_count += psx_apply_aggregate_member_declaration(
          &layout,
          &(psx_aggregate_member_declaration_request_t){
              .semantic_context = semantic_context,
              .target_tag_kind = tag_kind,
              .target_tag_name = tag_name,
              .target_tag_name_len = tag_len,
              .base_type = member_base_type,
              .declarator_shape = &resolved_shape,
              .member_name = head->identifier
                                 ? head->identifier->str
                                 : NULL,
              .member_name_len = head->identifier
                                     ? head->identifier->len
                                     : 0,
              .has_bitfield = head->has_bitfield,
              .bit_width = resolved_bit_width,
              .pack_alignment = declaration->pack_alignment,
              .requested_alignment = requested_alignment,
          },
          head->diagnostic_token);
    }
  }
  *out_size = psx_aggregate_layout_size(&layout);
  if (out_align) *out_align = psx_aggregate_layout_alignment(&layout);
  return member_count;
}

static void apply_decl_tag_action(
    const psx_parsed_tag_action_t *action,
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry) {
  if (!action || action->action == PSX_PARSED_TAG_NONE) return;
  psx_apply_parsed_tag_declaration_in_contexts(
      semantic_context, local_registry,
      action->kind, action->name, action->name_len,
      PSX_TAG_DECLARATION_REFERENCE, 0, 0, 0,
      action->diagnostic_token);
  if (action->action != PSX_PARSED_TAG_DEFINITION) return;

  int member_count = 0;
  int size = 0;
  int alignment = 0;
  if (action->kind == TK_ENUM) {
    member_count = psx_apply_parsed_enum_body_in_contexts(
        semantic_context, global_registry, local_registry,
        action->enum_body);
    size = 4;
    alignment = 4;
  } else {
    member_count = psx_apply_parsed_aggregate_body_layout_in_contexts(
        semantic_context, global_registry, local_registry,
        action->aggregate_body, action->kind,
        action->name, action->name_len, &size, &alignment);
  }
  psx_apply_parsed_tag_declaration_in_contexts(
      semantic_context, local_registry,
      action->kind, action->name, action->name_len,
      PSX_TAG_DECLARATION_DEFINITION, member_count, size, alignment,
      action->diagnostic_token);
}

static psx_type_t *build_parsed_type_name(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_parsed_type_name_t *type_name) {
  if (!type_name) return NULL;
  psx_type_t *base_type = NULL;
  if (type_name->atomic_inner) {
    base_type = build_parsed_type_name(
        semantic_context, global_registry, local_registry,
        type_name->atomic_inner);
    if (!base_type) return NULL;
    base_type->is_atomic = 1;
  } else {
    apply_decl_tag_action(
        &type_name->specifier.tag_action, semantic_context,
        global_registry, local_registry);
    base_type = psx_build_decl_specifier_type_in_context(
        semantic_context, &type_name->specifier);
    if (!base_type) return NULL;
  }

  psx_declarator_shape_t shape;
  ps_declarator_shape_init(&shape);
  psx_apply_parsed_declarator_in_contexts(
      semantic_context, global_registry, local_registry,
      &type_name->declarator, &shape, NULL);
  return psx_build_decl_type(
      &(psx_decl_type_request_t){
          .semantic_context = semantic_context,
          .base_type = base_type,
          .declarator_shape = &shape,
      });
}

const psx_type_t *psx_apply_parsed_type_name_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_parsed_type_name_t *type_name) {
  if (!semantic_context || !global_registry || !local_registry) return NULL;
  return build_parsed_type_name(
      semantic_context, global_registry, local_registry, type_name);
}

const psx_type_t *psx_apply_parsed_declarator_type_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_type_t *base_type,
    const psx_parsed_declarator_t *declarator) {
  if (!semantic_context || !global_registry || !local_registry ||
      !base_type || !declarator) return NULL;
  psx_declarator_shape_t shape;
  ps_declarator_shape_init(&shape);
  psx_apply_parsed_declarator_in_contexts(
      semantic_context, global_registry, local_registry,
      declarator, &shape, NULL);
  return psx_resolve_decl_type(
      &(psx_decl_type_request_t){
          .semantic_context = semantic_context,
          .base_type = base_type,
          .declarator_shape = &shape,
      });
}

const psx_type_t *psx_apply_runtime_declarator_type_in_context(
    psx_semantic_context_t *semantic_context,
    const psx_type_t *base_type,
    const psx_runtime_declarator_application_t *application) {
  if (!base_type || !application) return NULL;
  return psx_resolve_decl_type(
      &(psx_decl_type_request_t){
          .semantic_context = semantic_context,
          .base_type = base_type,
          .declarator_shape = &application->shape,
      });
}

void psx_begin_declaration_phase(
    psx_declaration_phase_t *phase,
    psx_parsed_decl_specifier_t *syntax) {
  if (!phase || !syntax) return;
  *phase = (psx_declaration_phase_t){0};
  phase->syntax = *syntax;
  *syntax = (psx_parsed_decl_specifier_t){0};
  phase->state = PSX_DECLARATION_PHASE_SYNTAX;
}

int psx_apply_declaration_phase_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_declaration_phase_t *phase, int standalone_tag) {
  if (!semantic_context || !global_registry || !local_registry || !phase ||
      phase->state != PSX_DECLARATION_PHASE_SYNTAX) return 0;
  phase->requested_alignment =
      psx_apply_parsed_decl_alignment(
          semantic_context, &phase->syntax);
  if (standalone_tag &&
      phase->syntax.source == PSX_PARSED_DECL_TYPE_TAG) {
    psx_apply_parsed_standalone_tag_in_contexts(
        semantic_context, global_registry, local_registry, &phase->syntax);
    phase->state = PSX_DECLARATION_PHASE_STANDALONE_TAG;
    return 1;
  }
  phase->base_type = psx_apply_parsed_decl_specifier_in_contexts(
      semantic_context, global_registry, local_registry, &phase->syntax);
  if (!phase->base_type) return 0;
  phase->state = PSX_DECLARATION_PHASE_RESOLVED_TYPE;
  return 1;
}

void psx_dispose_declaration_phase(psx_declaration_phase_t *phase) {
  if (!phase) return;
  ps_dispose_decl_specifier_syntax(&phase->syntax);
  *phase = (psx_declaration_phase_t){0};
}

const psx_type_t *psx_apply_parsed_decl_specifier_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_parsed_decl_specifier_t *specifier) {
  if (!semantic_context || !global_registry || !local_registry || !specifier)
    return NULL;
  apply_decl_tag_action(
      &specifier->tag_action, semantic_context,
      global_registry, local_registry);
  return psx_build_decl_specifier_type_in_context(
      semantic_context, specifier);
}

int psx_apply_parsed_decl_alignment(
    psx_semantic_context_t *semantic_context,
    const psx_parsed_decl_specifier_t *specifier) {
  if (!semantic_context || !specifier) return 0;
  int alignment = 0;
  for (int i = 0; i < specifier->alignas_expression_count; i++) {
    const psx_parsed_const_expr_t *expression =
        &specifier->alignas_expressions[i];
    if (!expression->has_constant_value) {
      ps_diag_ctx_in(ps_ctx_diagnostics(semantic_context), expression->start, "declaration-application",
                   "alignment syntax value was not prepared");
    }
    int value = (int)expression->constant_value;
    if (value > alignment) alignment = value;
  }
  return alignment;
}

void psx_apply_parsed_standalone_tag_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_parsed_decl_specifier_t *specifier) {
  if (!semantic_context || !global_registry || !local_registry ||
      !specifier || specifier->source != PSX_PARSED_DECL_TYPE_TAG) return;
  const psx_parsed_tag_action_t *action = &specifier->tag_action;
  if (action->action == PSX_PARSED_TAG_DEFINITION) {
    apply_decl_tag_action(
        action, semantic_context, global_registry, local_registry);
    return;
  }
  psx_apply_parsed_tag_declaration_in_contexts(
      semantic_context, local_registry,
      action->kind, action->name, action->name_len,
      PSX_TAG_DECLARATION_FORWARD, 0, 0, 0,
      action->diagnostic_token);
}

void psx_apply_parsed_declarator_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_parsed_declarator_t *declarator,
    psx_declarator_shape_t *shape, int *bit_width) {
  if (!semantic_context || !global_registry || !local_registry ||
      !declarator || !shape) return;
  psx_resolve_declarator_syntax_in_context(
      semantic_context, declarator, shape, bit_width);
  for (int i = 0; i < declarator->function_suffix_count; i++) {
    const psx_parsed_function_suffix_t *suffix =
        &declarator->function_suffixes[i];
    if (suffix->declarator_op_index < 0 ||
        suffix->declarator_op_index >= shape->count ||
        shape->ops[suffix->declarator_op_index].kind !=
            PSX_DECL_OP_FUNCTION ||
        !suffix->parameters) {
      ps_diag_ctx_in(ps_ctx_diagnostics(semantic_context), declarator->diagnostic_token, "declarator-application",
                   "invalid deferred function suffix target");
    }
    psx_apply_parsed_function_parameters_in_contexts(
        semantic_context, global_registry, local_registry,
        suffix->parameters,
        &shape->ops[suffix->declarator_op_index],
        declarator->diagnostic_token);
  }
}

void psx_apply_runtime_parsed_declarator_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_parsed_declarator_t *declarator,
    psx_runtime_declarator_application_t *application) {
  psx_apply_runtime_parsed_declarator_ex_in_contexts(
      semantic_context, global_registry, local_registry,
      declarator, application, -1);
}

void psx_apply_runtime_parsed_declarator_ex_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_parsed_declarator_t *declarator,
    psx_runtime_declarator_application_t *application,
    int skipped_function_op_index) {
  if (!semantic_context || !global_registry || !local_registry ||
      !declarator || !application) return;
  *application = (psx_runtime_declarator_application_t){0};
  if (!ps_declarator_shape_copy_in(
          ps_ctx_arena(semantic_context),
          &application->shape, &declarator->declarator_shape)) {
    ps_diag_ctx_in(ps_ctx_diagnostics(semantic_context), declarator->diagnostic_token, "declarator-resolution",
                "invalid local declarator shape");
  }
  if (declarator->array_bound_count > 0) {
    application->array_bounds = arena_alloc_in(
        ps_ctx_arena(semantic_context),
        (size_t)declarator->array_bound_count *
        sizeof(*application->array_bounds));
  }
  for (int i = 0; i < declarator->array_bound_count; i++) {
    const psx_parsed_array_bound_t *parsed = &declarator->array_bounds[i];
    if (parsed->declarator_op_index < 0 ||
        parsed->declarator_op_index >= application->shape.count ||
        application->shape.ops[parsed->declarator_op_index].kind !=
            PSX_DECL_OP_ARRAY) {
      ps_diag_ctx_in(ps_ctx_diagnostics(semantic_context), parsed->expression.start, "declarator-resolution",
                   "invalid local array bound target");
    }
    node_t *expression = parsed->expression.node;
    if (!expression) {
      ps_diag_ctx_in(ps_ctx_diagnostics(semantic_context), parsed->expression.start, "declarator-resolution",
                   "runtime array bound syntax was not prepared");
    }
    expression = psx_bind_identifier_tree_in_contexts(
        semantic_context, global_registry, local_registry,
        expression, parsed->expression.start);
    psx_semantic_resolve_tree_in_contexts(
        semantic_context, global_registry, local_registry,
        expression, NULL, parsed->expression.start);
    int is_constant = 1;
    long long value = psx_eval_const_int(expression, &is_constant);
    if (is_constant && value < 0) {
      ps_diag_ctx_in(ps_ctx_diagnostics(semantic_context), parsed->expression.start, "decl", "%s",
                   diag_message_for_in(ps_ctx_diagnostics(semantic_context),
                       DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED));
    }
    if (is_constant && value == 0) {
      ps_ctx_record_unsupported_gnu_extension_warning_in(
          semantic_context,
          parsed->expression.start, "zero-length array");
    }
    if (!ps_declarator_shape_set_array_bound(
            &application->shape, parsed->declarator_op_index,
            is_constant ? (int)value : 0, !is_constant)) {
      ps_diag_ctx_in(ps_ctx_diagnostics(semantic_context), parsed->expression.start, "declarator-resolution",
                  "invalid local array bound target");
    }
    application->array_bounds[application->array_bound_count++] =
        (psx_runtime_array_bound_t){
            .declarator_op_index = parsed->declarator_op_index,
            .expression = expression,
            .constant_value = is_constant ? value : 0,
            .is_constant = is_constant,
        };
  }
  for (int i = 0; i < declarator->function_suffix_count; i++) {
    const psx_parsed_function_suffix_t *suffix =
        &declarator->function_suffixes[i];
    if (suffix->declarator_op_index < 0 ||
        suffix->declarator_op_index >= application->shape.count ||
        application->shape.ops[suffix->declarator_op_index].kind !=
            PSX_DECL_OP_FUNCTION) {
      ps_diag_ctx_in(ps_ctx_diagnostics(semantic_context), declarator->diagnostic_token, "declarator-resolution",
                   "invalid local function suffix target");
    }
    psx_declarator_op_t *function_op =
        &application->shape.ops[suffix->declarator_op_index];
    if (suffix->declarator_op_index == skipped_function_op_index) {
      (void)ps_declarator_op_set_variadic(
          function_op,
          suffix->parameters && suffix->parameters->is_variadic);
      continue;
    }
    psx_apply_parsed_function_parameters_in_contexts(
        semantic_context, global_registry, local_registry,
        suffix->parameters, function_op,
        declarator->diagnostic_token);
  }
}

void psx_apply_parsed_function_parameters_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parsed_function_parameters_t *parameters,
    psx_declarator_op_t *function_op, token_t *diagnostic_token) {
  if (!semantic_context || !global_registry || !local_registry) return;
  if (!parameters || !function_op ||
      function_op->kind != PSX_DECL_OP_FUNCTION) {
    ps_diag_ctx_in(ps_ctx_diagnostics(semantic_context), diagnostic_token, "declarator-application",
                 "invalid function parameter syntax target");
  }
  const psx_type_t **resolved_types = parameters->count > 0
      ? calloc((size_t)parameters->count, sizeof(*resolved_types)) : NULL;
  if (parameters->count > 0 && !resolved_types) {
    ps_diag_ctx_in(ps_ctx_diagnostics(semantic_context), diagnostic_token, "declarator-application",
                "function parameter type allocation failed");
  }
  int resolved_count = 0;
  ps_ctx_enter_block_scope_in(semantic_context);
  ps_decl_enter_scope_in(local_registry);
  for (int i = 0; i < parameters->count; i++) {
    psx_parsed_function_parameter_t *parameter = &parameters->items[i];
    const psx_type_t *base =
        psx_apply_parsed_decl_specifier_in_contexts(
            semantic_context, global_registry, local_registry,
            &parameter->specifier);
    if (!base) {
      ps_diag_ctx_in(ps_ctx_diagnostics(semantic_context), parameter->specifier.diagnostic_token, "param", "%s",
                   diag_message_for_in(ps_ctx_diagnostics(semantic_context), DIAG_ERR_PARSER_MEMBER_TYPE_REQUIRED));
    }
    psx_local_lookup_point_t parameter_lookup_point =
        ps_local_registry_capture_lookup_point_in(local_registry);
    for (int b = 0; b < parameter->declarator.array_bound_count; b++) {
      psx_parsed_const_expr_t *bound =
          &parameter->declarator.array_bounds[b].expression;
      bound->node = psx_bind_identifier_tree_at_lookup_point_in_contexts(
          semantic_context, global_registry, local_registry,
          parameter_lookup_point, bound->node, bound->start);
    }
    psx_runtime_declarator_application_t parameter_application;
    psx_apply_runtime_parsed_declarator_in_contexts(
        semantic_context, global_registry, local_registry,
        &parameter->declarator, &parameter_application);
    psx_type_t *type = psx_build_decl_type(
        &(psx_decl_type_request_t){
            .semantic_context = semantic_context,
            .base_type = base,
            .declarator_shape = &parameter_application.shape,
        });
    if (parameters->count == 1 && type && type->kind == PSX_TYPE_VOID &&
        parameter_application.shape.count == 0) {
      resolved_count = 0;
      break;
    }
    psx_type_t *adjusted = ps_type_adjust_parameter_type_in(
        ps_ctx_arena(semantic_context), type);
    resolved_types[resolved_count] = adjusted;
    resolved_count++;
    token_ident_t *name = parameter->declarator.identifier;
    if (name && !ps_local_registry_create_type_binding_in(
                    local_registry, name->str, name->len, adjusted,
                    (token_t *)name)) {
      ps_diag_ctx_in(ps_ctx_diagnostics(semantic_context), (token_t *)name, "param",
                  "prototype parameter binding failed");
    }
  }
  ps_decl_leave_scope_in(local_registry);
  ps_ctx_leave_block_scope_in(semantic_context);
  psx_set_resolved_function_parameter_types(
      ps_ctx_arena(semantic_context), function_op,
      resolved_types, resolved_count,
      parameters->is_variadic);
  free(resolved_types);
}
