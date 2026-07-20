#include "declaration_application.h"
#include "declaration_registration.h"

#include "../parser/arena.h"
#include "../parser/diag.h"
#include "../parser/declarator_shape_builder.h"
#include "../parser/enum_const.h"
#include "../parser/decl.h"
#include "../parser/global_registry.h"
#include "../parser/local_registry.h"
#include "../parser/node_utils.h"
#include "../parser/semantic_ctx.h"
#include "../type_layout.h"
#include "aggregate_member_resolution.h"
#include "declaration_resolution.h"
#include "declarator_bound_resolution.h"
#include "enum_constant_resolution.h"
#include "function_parameter_resolution.h"
#include "parameter_declaration_resolution.h"
#include "prototype_parameter.h"
#include "syntax_typed_hir_resolution.h"
#include "type_name_resolution.h"
#include "typed_hir_materialization.h"
#include "../diag/diag.h"
#include "../diag/error_catalog.h"

#include <limits.h>
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
    if (member->initializer &&
        !psx_resolve_enum_initializer_syntax_in_contexts(
            semantic_context, global_registry, local_registry,
            member->initializer, (token_t *)member->enumerator,
            &value))
      return i;
    psx_apply_parsed_enum_constant_in(
        semantic_context,
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
  const psx_record_decl_t *record = ps_ctx_ensure_tag_record_decl_in(
      semantic_context, tag_kind, tag_name, tag_len);
  if (!record || record->record_id == PSX_RECORD_ID_INVALID) return 0;
  int member_count = 0;
  psx_aggregate_layout_state_t layout;
  psx_aggregate_layout_init(&layout, record);
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
    psx_qual_type_t member_base_qual_type =
        psx_apply_parsed_decl_specifier_qual_type_in_contexts(
            semantic_context, global_registry, local_registry,
            &declaration->specifier);
    if (member_base_qual_type.type_id == PSX_TYPE_ID_INVALID) {
      ps_diag_ctx_in(ps_ctx_diagnostics(semantic_context), declaration->specifier.diagnostic_token, "decl", "%s",
                   diag_message_for_in(ps_ctx_diagnostics(semantic_context),
                       DIAG_ERR_PARSER_MEMBER_TYPE_REQUIRED));
    }
    int requested_alignment =
        psx_resolve_parsed_decl_alignment_in_contexts(
            semantic_context, global_registry, local_registry,
            &declaration->specifier);
    for (int j = 0; j < declaration->declarator_count; j++) {
      psx_parsed_declarator_t *head = &declaration->declarators[j];
      psx_declarator_shape_t resolved_shape;
      int resolved_bit_width = 0;
      psx_apply_parsed_declarator_in_contexts(
          semantic_context, global_registry, local_registry,
          head, &resolved_shape, &resolved_bit_width);
      member_count += psx_apply_aggregate_member_declaration(
          &layout,
          &(psx_aggregate_member_declaration_request_t){
              .semantic_context = semantic_context,
              .base_qual_type = member_base_qual_type,
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
  psx_apply_parsed_tag_declaration_in(
      semantic_context,
      action->kind, action->name, action->name_len,
      action->action == PSX_PARSED_TAG_DEFINITION
          ? PSX_TAG_DECLARATION_FORWARD
          : PSX_TAG_DECLARATION_REFERENCE,
      0,
      action->diagnostic_token);
  if (action->action != PSX_PARSED_TAG_DEFINITION) return;

  int member_count = 0;
  int size = 0;
  int alignment = 1;
  if (action->kind == TK_ENUM) {
    member_count = psx_apply_parsed_enum_body_in_contexts(
        semantic_context, global_registry, local_registry,
        action->enum_body);
  } else {
    member_count = psx_apply_parsed_aggregate_body_layout_in_contexts(
        semantic_context, global_registry, local_registry,
        action->aggregate_body, action->kind,
        action->name, action->name_len, &size, &alignment);
  }
  psx_apply_parsed_tag_declaration_in(
      semantic_context,
      action->kind, action->name, action->name_len,
      PSX_TAG_DECLARATION_DEFINITION, member_count,
      action->diagnostic_token);
  if (action->kind == TK_STRUCT || action->kind == TK_UNION) {
    const psx_record_decl_t *record = ps_ctx_ensure_tag_record_decl_in(
        semantic_context, action->kind, action->name, action->name_len);
    if (record)
      (void)ps_ctx_publish_record_layout_in(
          semantic_context, record->record_id, size, alignment);
  }
}

static psx_qual_type_t resolve_parsed_type_name_qual_type(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_parsed_type_name_t *type_name) {
  if (!type_name)
    return (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                             PSX_TYPE_QUALIFIER_NONE};
  psx_qual_type_t base_qual_type = {
      PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  if (type_name->atomic_inner) {
    base_qual_type = resolve_parsed_type_name_qual_type(
        semantic_context, global_registry, local_registry,
        type_name->atomic_inner);
    if (base_qual_type.type_id == PSX_TYPE_ID_INVALID)
      return base_qual_type;
    base_qual_type.qualifiers |= PSX_TYPE_QUALIFIER_ATOMIC;
  } else {
    apply_decl_tag_action(
        &type_name->specifier.tag_action, semantic_context,
        global_registry, local_registry);
    base_qual_type = psx_resolve_decl_specifier_qual_type_in_context(
        semantic_context, &type_name->specifier);
    if (base_qual_type.type_id == PSX_TYPE_ID_INVALID)
      return base_qual_type;
  }

  psx_declarator_shape_t shape;
  ps_declarator_shape_init(&shape);
  psx_apply_parsed_declarator_in_contexts(
      semantic_context, global_registry, local_registry,
      &type_name->declarator, &shape, NULL);
  return psx_resolve_decl_qual_type(
      &(psx_decl_type_request_t){
          .semantic_context = semantic_context,
          .base_qual_type = base_qual_type,
          .declarator_shape = &shape,
      });
}

const psx_type_t *psx_apply_parsed_type_name_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_parsed_type_name_t *type_name) {
  if (!semantic_context || !global_registry || !local_registry) return NULL;
  psx_qual_type_t resolved = resolve_parsed_type_name_qual_type(
      semantic_context, global_registry, local_registry, type_name);
  return psx_semantic_type_table_lookup_qual_type(
      ps_ctx_semantic_type_table_in(semantic_context), resolved);
}

const psx_type_t *psx_apply_parsed_declarator_type_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_type_t *base_type,
    const psx_parsed_declarator_t *declarator) {
  if (!semantic_context || !global_registry || !local_registry ||
      !base_type || !declarator) return NULL;
  psx_qual_type_t resolved =
      psx_apply_parsed_declarator_qual_type_in_contexts(
          semantic_context, global_registry, local_registry,
          ps_ctx_intern_declaration_qual_type_in(
              semantic_context, base_type),
          declarator);
  return psx_semantic_type_table_lookup_qual_type(
      ps_ctx_semantic_type_table_in(semantic_context), resolved);
}

psx_qual_type_t psx_apply_parsed_declarator_qual_type_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_qual_type_t base_qual_type,
    const psx_parsed_declarator_t *declarator) {
  if (!semantic_context || !global_registry || !local_registry ||
      base_qual_type.type_id == PSX_TYPE_ID_INVALID || !declarator) {
    return (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                             PSX_TYPE_QUALIFIER_NONE};
  }
  psx_declarator_shape_t shape;
  ps_declarator_shape_init(&shape);
  psx_apply_parsed_declarator_in_contexts(
      semantic_context, global_registry, local_registry,
      declarator, &shape, NULL);
  return psx_resolve_decl_qual_type(
      &(psx_decl_type_request_t){
          .semantic_context = semantic_context,
          .base_qual_type = base_qual_type,
          .declarator_shape = &shape,
      });
}

const psx_type_t *psx_apply_runtime_declarator_type_in_context(
    psx_semantic_context_t *semantic_context,
    const psx_type_t *base_type,
    const psx_runtime_declarator_application_t *application) {
  if (!base_type || !application) return NULL;
  psx_qual_type_t resolved =
      psx_apply_runtime_declarator_qual_type_in_context(
          semantic_context,
          ps_ctx_intern_declaration_qual_type_in(
              semantic_context, base_type),
          application);
  return psx_semantic_type_table_lookup_qual_type(
      ps_ctx_semantic_type_table_in(semantic_context), resolved);
}

psx_qual_type_t psx_apply_runtime_declarator_qual_type_in_context(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t base_qual_type,
    const psx_runtime_declarator_application_t *application) {
  if (!semantic_context ||
      base_qual_type.type_id == PSX_TYPE_ID_INVALID || !application) {
    return (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                             PSX_TYPE_QUALIFIER_NONE};
  }
  return psx_resolve_decl_qual_type(
      &(psx_decl_type_request_t){
          .semantic_context = semantic_context,
          .base_qual_type = base_qual_type,
          .declarator_shape = &application->shape,
      });
}

int psx_compose_runtime_declarator_applications_in(
    arena_context_t *arena_context,
    const psx_runtime_declarator_application_t *declared,
    const psx_runtime_declarator_application_t *typedef_base,
    psx_runtime_declarator_application_t *result) {
  if (result) *result = (psx_runtime_declarator_application_t){0};
  if (!arena_context || !declared || !result ||
      declared->shape.count < 0 || declared->array_bound_count < 0 ||
      (declared->array_bound_count > 0 && !declared->array_bounds) ||
      (typedef_base &&
       (typedef_base->shape.count < 0 ||
        typedef_base->array_bound_count < 0 ||
        (typedef_base->array_bound_count > 0 &&
         !typedef_base->array_bounds))))
    return 0;
  if (!ps_declarator_shape_copy_in(
          arena_context, &result->shape, &declared->shape))
    return 0;
  if (typedef_base &&
      !ps_declarator_shape_append_shape_in(
          arena_context, &result->shape, &typedef_base->shape))
    return 0;
  int base_bound_count =
      typedef_base ? typedef_base->array_bound_count : 0;
  if (declared->array_bound_count >
      INT_MAX - base_bound_count)
    return 0;
  int bound_count = declared->array_bound_count + base_bound_count;
  if (bound_count > 0) {
    result->array_bounds = arena_alloc_in(
        arena_context,
        (size_t)bound_count * sizeof(*result->array_bounds));
    if (!result->array_bounds) return 0;
  }
  for (int i = 0; i < declared->array_bound_count; i++) {
    const psx_runtime_array_bound_t *bound =
        &declared->array_bounds[i];
    if (bound->declarator_op_index < 0 ||
        bound->declarator_op_index >= declared->shape.count)
      return 0;
    result->array_bounds[result->array_bound_count++] = *bound;
  }
  int op_offset = declared->shape.count;
  for (int i = 0; i < base_bound_count; i++) {
    psx_runtime_array_bound_t bound =
        typedef_base->array_bounds[i];
    if (bound.declarator_op_index < 0 ||
        bound.declarator_op_index >= typedef_base->shape.count ||
        bound.declarator_op_index > INT_MAX - op_offset)
      return 0;
    bound.declarator_op_index += op_offset;
    result->array_bounds[result->array_bound_count++] = bound;
  }
  return 1;
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
      psx_resolve_parsed_decl_alignment_in_contexts(
          semantic_context, global_registry, local_registry,
          &phase->syntax);
  if (standalone_tag &&
      phase->syntax.source == PSX_PARSED_DECL_TYPE_TAG) {
    psx_apply_parsed_standalone_tag_in_contexts(
        semantic_context, global_registry, local_registry, &phase->syntax);
    phase->state = PSX_DECLARATION_PHASE_STANDALONE_TAG;
    return 1;
  }
  phase->base_qual_type =
      psx_apply_parsed_decl_specifier_qual_type_in_contexts(
          semantic_context, global_registry, local_registry,
          &phase->syntax);
  if (phase->base_qual_type.type_id == PSX_TYPE_ID_INVALID) return 0;
  phase->type_table = ps_ctx_semantic_type_table_in(semantic_context);
  phase->state = PSX_DECLARATION_PHASE_RESOLVED_TYPE;
  return 1;
}

void psx_dispose_declaration_phase(psx_declaration_phase_t *phase) {
  if (!phase) return;
  ps_dispose_decl_specifier_syntax(&phase->syntax);
  *phase = (psx_declaration_phase_t){0};
}

psx_qual_type_t psx_declaration_phase_base_qual_type(
    const psx_declaration_phase_t *phase) {
  return phase && phase->state == PSX_DECLARATION_PHASE_RESOLVED_TYPE
             ? phase->base_qual_type
             : (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                                 PSX_TYPE_QUALIFIER_NONE};
}

const psx_type_t *psx_declaration_phase_base_type(
    const psx_declaration_phase_t *phase) {
  return phase && phase->state == PSX_DECLARATION_PHASE_RESOLVED_TYPE
             ? psx_semantic_type_table_lookup_qual_type(
                   phase->type_table, phase->base_qual_type)
             : NULL;
}

psx_qual_type_t psx_apply_parsed_decl_specifier_qual_type_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_parsed_decl_specifier_t *specifier) {
  if (!semantic_context || !global_registry || !local_registry || !specifier)
    return (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                             PSX_TYPE_QUALIFIER_NONE};
  apply_decl_tag_action(
      &specifier->tag_action, semantic_context,
      global_registry, local_registry);
  return psx_resolve_decl_specifier_qual_type_in_context(
      semantic_context, specifier);
}

static int resolve_parsed_alignas_expression(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_parsed_alignas_t *alignas) {
  psx_scope_lookup_point_t point = {
      .scope_id = alignas->scope_seq,
      .declaration_order = alignas->declaration_seq,
  };
  const psx_typed_hir_tree_t *typed_hir = NULL;
  psx_syntax_integer_constant_result_t constant_result;
  psx_resolved_hir_build_failure_t failure;
  psx_syntax_typed_hir_resolution_status_t status =
      psx_resolve_syntax_integer_constant_expression_direct_to_typed_hir_in_contexts(
          semantic_context, global_registry, local_registry, &point,
          alignas->expression, &typed_hir, &constant_result, &failure);
  if (status == PSX_SYNTAX_TYPED_HIR_RESOLVED && typed_hir &&
      constant_result.is_constant) {
    if (constant_result.value > INT_MAX) {
      ps_diag_ctx_in(
          ps_ctx_diagnostics(semantic_context), alignas->diagnostic_token,
          "alignas", "alignment is not representable as int");
    }
    return constant_result.value > 0 ? (int)constant_result.value : 1;
  }
  ag_diagnostic_context_t *diagnostics =
      ps_ctx_diagnostics(semantic_context);
  if (status == PSX_SYNTAX_TYPED_HIR_FAILED) {
    diag_emit_internalf_in(
        diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED,
        "%s: declaration alignas direct Typed HIR resolution failed "
        "(status %d, node kind %d)",
        diag_message_for_in(
            diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED),
        (int)failure.status, failure.source_node_kind);
    return 1;
  }
  ps_diag_ctx_in(
      diagnostics, alignas->diagnostic_token, "alignas",
      "alignment is not an integer constant expression");
  return 1;
}

static int resolve_parsed_alignas_type_name(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_parsed_alignas_t *alignas) {
  psx_qual_type_t qual_type;
  psx_type_name_ref_t type_name = {
      .syntax = alignas->type_name,
      .scope_seq = alignas->scope_seq,
      .declaration_seq = alignas->declaration_seq,
  };
  if (!psx_resolve_type_name_qual_type_in_contexts(
          semantic_context, global_registry, local_registry,
          &type_name, &qual_type)) {
    ps_diag_ctx_in(
        ps_ctx_diagnostics(semantic_context), alignas->diagnostic_token,
        "alignas", "alignment type name could not be resolved");
    return 1;
  }
  int alignment = ps_type_alignof_id(
      ps_ctx_semantic_type_table_in(semantic_context),
      ps_ctx_record_layout_table_in(semantic_context), qual_type.type_id,
      ps_ctx_data_layout(semantic_context));
  return alignment > 0 ? alignment : 1;
}

int psx_resolve_parsed_decl_alignment_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_parsed_decl_specifier_t *specifier) {
  if (!semantic_context || !global_registry || !local_registry ||
      !specifier)
    return 0;
  int alignment = 0;
  for (int i = 0; i < specifier->alignas_specifier_count; i++) {
    const psx_parsed_alignas_t *alignas =
        &specifier->alignas_specifiers[i];
    int value = alignas->kind == PSX_PARSED_ALIGNAS_TYPE_NAME
                    ? resolve_parsed_alignas_type_name(
                          semantic_context, global_registry,
                          local_registry, alignas)
                    : resolve_parsed_alignas_expression(
                          semantic_context, global_registry,
                          local_registry, alignas);
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
  psx_apply_parsed_tag_declaration_in(
      semantic_context,
      action->kind, action->name, action->name_len,
      PSX_TAG_DECLARATION_FORWARD, 0,
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
      semantic_context, global_registry, local_registry,
      declarator, shape, bit_width);
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

static int apply_resolved_runtime_parsed_declarator(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_parsed_declarator_t *declarator,
    psx_runtime_declarator_application_t *application,
    const psx_runtime_array_bound_t *resolved_bounds,
    int resolved_bound_count,
    int skipped_function_op_index) {
  if (!semantic_context || !global_registry || !local_registry ||
      !declarator || !application || resolved_bound_count < 0 ||
      resolved_bound_count != declarator->array_bound_count ||
      (resolved_bound_count > 0 && !resolved_bounds))
    return 0;
  *application = (psx_runtime_declarator_application_t){0};
  if (!ps_declarator_shape_copy_in(
          ps_ctx_arena(semantic_context),
          &application->shape, &declarator->declarator_shape)) {
    ps_diag_ctx_in(ps_ctx_diagnostics(semantic_context), declarator->diagnostic_token, "declarator-resolution",
                "invalid local declarator shape");
  }
  if (resolved_bound_count > 0) {
    application->array_bounds = arena_alloc_in(
        ps_ctx_arena(semantic_context),
        (size_t)resolved_bound_count *
        sizeof(*application->array_bounds));
    if (!application->array_bounds) return 0;
  }
  for (int i = 0; i < resolved_bound_count; i++) {
    const psx_parsed_array_bound_t *parsed = &declarator->array_bounds[i];
    const psx_runtime_array_bound_t *resolved = &resolved_bounds[i];
    if (parsed->declarator_op_index < 0 ||
        parsed->declarator_op_index >= application->shape.count ||
        application->shape.ops[parsed->declarator_op_index].kind !=
            PSX_DECL_OP_ARRAY ||
        resolved->declarator_op_index != parsed->declarator_op_index) {
      ps_diag_ctx_in(ps_ctx_diagnostics(semantic_context), parsed->expression.start, "declarator-resolution",
                   "invalid local array bound target");
    }
    int is_constant = resolved->is_constant;
    long long value = resolved->constant_value;
    if (is_constant && value < 0) {
      ps_diag_ctx_in(ps_ctx_diagnostics(semantic_context), parsed->expression.start, "decl", "%s",
                   diag_message_for_in(ps_ctx_diagnostics(semantic_context),
                       DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED));
    }
    if (is_constant && value == 0) {
      ps_ctx_record_unsupported_gnu_extension_in(
          semantic_context,
          parsed->expression.start, "zero-length array");
    }
    if (!ps_declarator_shape_set_array_bound(
            &application->shape, parsed->declarator_op_index,
            is_constant ? (int)value : 0, !is_constant)) {
      ps_diag_ctx_in(ps_ctx_diagnostics(semantic_context), parsed->expression.start, "declarator-resolution",
                  "invalid local array bound target");
    }
    application->array_bounds[application->array_bound_count++] = *resolved;
    if (!is_constant &&
        resolved->expression_id == PSX_SEMANTIC_EXPR_ID_INVALID) {
      ps_diag_ctx_in(
          ps_ctx_diagnostics(semantic_context), parsed->expression.start,
          "declarator-resolution",
          "runtime array bound expression registration failed");
    }
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
  return 1;
}

int psx_apply_resolved_runtime_parsed_declarator_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_parsed_declarator_t *declarator,
    const psx_runtime_array_bound_t *resolved_bounds,
    int resolved_bound_count,
    psx_runtime_declarator_application_t *application) {
  return apply_resolved_runtime_parsed_declarator(
      semantic_context, global_registry, local_registry,
      declarator, application, resolved_bounds,
      resolved_bound_count, -1);
}

static void apply_runtime_parsed_declarator(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_parsed_declarator_t *declarator,
    psx_runtime_declarator_application_t *application,
    int skipped_function_op_index,
    const psx_scope_lookup_point_t *lookup_point) {
  if (!semantic_context || !global_registry || !local_registry ||
      !declarator || !application) return;
  psx_runtime_array_bound_t *resolved_bounds = NULL;
  if (declarator->array_bound_count > 0) {
    resolved_bounds = arena_alloc_in(
        ps_ctx_arena(semantic_context),
        (size_t)declarator->array_bound_count *
        sizeof(*resolved_bounds));
    if (!resolved_bounds) return;
  }
  for (int i = 0; i < declarator->array_bound_count; i++) {
    const psx_parsed_array_bound_t *parsed = &declarator->array_bounds[i];
    const node_t *syntax_expression = parsed->expression.node;
    if (!syntax_expression) {
      ps_diag_ctx_in(ps_ctx_diagnostics(semantic_context), parsed->expression.start, "declarator-resolution",
                   "runtime array bound syntax was not prepared");
    }
    psx_declarator_bound_resolution_t bound_resolution;
    if (!psx_resolve_declarator_bound_in_contexts(
            semantic_context, global_registry, local_registry,
            syntax_expression, lookup_point,
            parsed->expression.start, &bound_resolution)) {
      ps_diag_ctx_in(
          ps_ctx_diagnostics(semantic_context),
          parsed->expression.start, "declarator-resolution",
          "runtime array bound resolution failed");
    }
    resolved_bounds[i] = (psx_runtime_array_bound_t){
        .declarator_op_index = parsed->declarator_op_index,
        .expression_id = ps_ctx_register_semantic_expression_in(
            semantic_context, bound_resolution.typed_expression),
        .constant_value = bound_resolution.is_constant
                              ? bound_resolution.constant_value : 0,
        .is_constant = bound_resolution.is_constant,
    };
    if (resolved_bounds[i].expression_id ==
        PSX_SEMANTIC_EXPR_ID_INVALID) {
      ps_diag_ctx_in(
          ps_ctx_diagnostics(semantic_context), parsed->expression.start,
          "declarator-resolution",
          "runtime array bound expression registration failed");
    }
  }
  (void)apply_resolved_runtime_parsed_declarator(
      semantic_context, global_registry, local_registry,
      declarator, application, resolved_bounds,
      declarator->array_bound_count, skipped_function_op_index);
}

void psx_apply_runtime_parsed_declarator_ex_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_parsed_declarator_t *declarator,
    psx_runtime_declarator_application_t *application,
    int skipped_function_op_index) {
  apply_runtime_parsed_declarator(
      semantic_context, global_registry, local_registry,
      declarator, application, skipped_function_op_index, NULL);
}

void psx_apply_runtime_parsed_declarator_at_lookup_point_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_parsed_declarator_t *declarator,
    psx_runtime_declarator_application_t *application,
    int skipped_function_op_index,
    psx_scope_lookup_point_t lookup_point) {
  apply_runtime_parsed_declarator(
      semantic_context, global_registry, local_registry,
      declarator, application, skipped_function_op_index,
      &lookup_point);
}

void psx_apply_parsed_function_parameters_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_parsed_function_parameters_t *parameters,
    psx_declarator_op_t *function_op, token_t *diagnostic_token) {
  if (!semantic_context || !global_registry || !local_registry) return;
  if (!parameters || !function_op ||
      function_op->kind != PSX_DECL_OP_FUNCTION) {
    ps_diag_ctx_in(ps_ctx_diagnostics(semantic_context), diagnostic_token, "declarator-application",
                 "invalid function parameter syntax target");
  }
  psx_qual_type_t *resolved_qual_types = parameters->count > 0
      ? calloc(
            (size_t)parameters->count, sizeof(*resolved_qual_types))
      : NULL;
  if (parameters->count > 0 && !resolved_qual_types) {
    ps_diag_ctx_in(ps_ctx_diagnostics(semantic_context), diagnostic_token, "declarator-application",
                "function parameter type allocation failed");
  }
  int resolved_count = 0;
  psx_scope_graph_t *scope_graph =
      ps_ctx_scope_graph(semantic_context);
  if (!scope_graph ||
      psx_scope_graph_enter_scope(
          scope_graph, PSX_SCOPE_FUNCTION_PROTOTYPE) ==
          PSX_SCOPE_ID_INVALID) {
    ps_diag_ctx_in(
        ps_ctx_diagnostics(semantic_context), diagnostic_token,
        "declarator-application",
        "prototype scope allocation failed");
    free(resolved_qual_types);
    return;
  }
  for (int i = 0; i < parameters->count; i++) {
    const psx_parsed_function_parameter_t *parameter =
        &parameters->items[i];
    psx_qual_type_t base_qual_type =
        psx_apply_parsed_decl_specifier_qual_type_in_contexts(
            semantic_context, global_registry, local_registry,
            &parameter->specifier);
    if (base_qual_type.type_id == PSX_TYPE_ID_INVALID) {
      ps_diag_ctx_in(ps_ctx_diagnostics(semantic_context), parameter->specifier.diagnostic_token, "param", "%s",
                   diag_message_for_in(ps_ctx_diagnostics(semantic_context), DIAG_ERR_PARSER_MEMBER_TYPE_REQUIRED));
    }
    psx_type_shape_t base_shape = {0};
    if (!psx_semantic_type_table_describe(
            ps_ctx_semantic_type_table_in(semantic_context),
            base_qual_type.type_id, &base_shape)) {
      ps_diag_ctx_in(
          ps_ctx_diagnostics(semantic_context),
          parameter->specifier.diagnostic_token, "param",
          "canonical parameter base type description failed");
    }
    psx_scope_lookup_point_t parameter_lookup_point =
        psx_scope_graph_capture_lookup_point(
            ps_ctx_scope_graph(semantic_context));
    psx_runtime_declarator_application_t parameter_application;
    psx_apply_runtime_parsed_declarator_at_lookup_point_in_contexts(
        semantic_context, global_registry, local_registry,
        &parameter->declarator, &parameter_application, -1,
        parameter_lookup_point);
    if (parameters->count == 1 && base_shape.kind == PSX_TYPE_VOID &&
        parameter_application.shape.count == 0) {
      resolved_count = 0;
      break;
    }
    psx_parameter_declaration_resolution_t parameter_resolution;
    if (!psx_resolve_parameter_declaration(
            &(psx_parameter_declaration_resolution_request_t){
                .type = {
                    .semantic_context = semantic_context,
                    .base_qual_type = base_qual_type,
                    .declarator_shape = &parameter_application.shape,
                },
            },
            &parameter_resolution)) {
      ps_diag_ctx_in(
          ps_ctx_diagnostics(semantic_context),
          parameter->specifier.diagnostic_token, "param",
          "canonical prototype parameter resolution failed");
    }
    psx_qual_type_t adjusted_qual_type =
        parameter_resolution.function_qual_type;
    resolved_qual_types[resolved_count] = adjusted_qual_type;
    resolved_count++;
    token_ident_t *name = parameter->declarator.identifier;
    if (name && !psx_declare_prototype_parameter_in(
                    semantic_context, name->str, name->len,
                    parameter_resolution.declaration_qual_type,
                    (token_t *)name)) {
      ps_diag_ctx_in(ps_ctx_diagnostics(semantic_context), (token_t *)name, "param",
                  "prototype parameter binding failed");
    }
  }
  if (!psx_scope_graph_leave_scope(scope_graph)) {
    ps_diag_ctx_in(
        ps_ctx_diagnostics(semantic_context), diagnostic_token,
        "declarator-application",
        "prototype scope finalization failed");
  }
  psx_set_resolved_function_parameter_qual_types(
      ps_ctx_arena(semantic_context), function_op,
      resolved_qual_types, resolved_count,
      parameters->is_variadic,
      parameters->count > 0 || parameters->is_variadic);
  free(resolved_qual_types);
}
