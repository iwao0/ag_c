#include "declaration_specifier_resolution.h"

#include "aggregate_member_resolution.h"
#include "declaration_application.h"
#include "declaration_registration.h"
#include "declarator_bound_resolution.h"
#include "enum_constant_resolution.h"
#include "../parser/aggregate_member_syntax.h"
#include "../parser/arena.h"
#include "../parser/diag.h"
#include "../parser/semantic_ctx.h"

#include <limits.h>
#include <string.h>

typedef struct {
  psx_semantic_context_t *semantic_context;
  psx_global_registry_t *global_registry;
  psx_local_registry_t *local_registry;
} decl_specifier_value_context_t;

static int decl_specifier_syntax_supported(
    const psx_parsed_decl_specifier_t *specifier);

static int tag_action_syntax_supported(
    const psx_parsed_tag_action_t *action) {
  if (!action || action->action == PSX_PARSED_TAG_NONE ||
      action->action == PSX_PARSED_TAG_REFERENCE)
    return 1;
  if (action->action != PSX_PARSED_TAG_DEFINITION) return 0;
  if (action->kind == TK_ENUM)
    return action->enum_body != NULL;
  if ((action->kind != TK_STRUCT && action->kind != TK_UNION) ||
      !action->aggregate_body)
    return 0;
  const psx_parsed_aggregate_body_t *body = action->aggregate_body;
  for (int i = 0; i < body->item_count; i++) {
    const psx_parsed_aggregate_item_t *item = &body->items[i];
    if (item->kind != PSX_PARSED_AGGREGATE_MEMBER_DECLARATION)
      return 0;
    const psx_parsed_aggregate_member_declaration_t *declaration =
        &item->value.member_declaration;
    if (!decl_specifier_syntax_supported(&declaration->specifier))
      return 0;
    for (int j = 0; j < declaration->declarator_count; j++) {
      const psx_parsed_declarator_t *declarator =
          &declaration->declarators[j];
      for (int bound = 0; bound < declarator->array_bound_count;
           bound++) {
        const psx_parsed_const_expr_t *expression =
            &declarator->array_bounds[bound].expression;
        if (!expression->node)
          return 0;
      }
      if (declarator->has_bitfield &&
          !declarator->bit_width_expression.node)
        return 0;
    }
  }
  return 1;
}

static int decl_specifier_syntax_supported(
    const psx_parsed_decl_specifier_t *specifier) {
  if (!specifier || specifier->alignas_specifier_count < 0 ||
      specifier->alignas_specifier_count > 8 ||
      !tag_action_syntax_supported(&specifier->tag_action))
    return 0;
  for (int i = 0; i < specifier->alignas_specifier_count; i++) {
    const psx_parsed_alignas_t *alignas =
        &specifier->alignas_specifiers[i];
    if ((alignas->kind == PSX_PARSED_ALIGNAS_EXPRESSION &&
         !alignas->expression) ||
        (alignas->kind == PSX_PARSED_ALIGNAS_TYPE_NAME &&
         !alignas->type_name))
      return 0;
  }
  return 1;
}

static long long resolve_const_expr_value(
    const decl_specifier_value_context_t *context,
    const psx_parsed_const_expr_t *expression) {
  psx_declarator_bound_resolution_t resolution;
  if (!expression || !expression->node ||
      !psx_resolve_declarator_bound_in_contexts(
          context->semantic_context, context->global_registry,
          context->local_registry, expression->node, NULL,
          expression->start, &resolution) ||
      !resolution.is_constant) {
    ps_diag_ctx_in(
        ps_ctx_diagnostics(context->semantic_context),
        expression ? expression->start : NULL,
        "declaration-specifier",
        "declarator expression is not an integer constant expression");
    return 0;
  }
  return resolution.constant_value;
}

static int resolve_decl_alignment_value(
    const decl_specifier_value_context_t *context,
    const psx_parsed_decl_specifier_t *specifier) {
  return psx_resolve_parsed_decl_alignment_in_contexts(
      context->semantic_context, context->global_registry,
      context->local_registry, specifier);
}

static psx_decl_specifier_value_status_t resolve_tag_action_value(
    const decl_specifier_value_context_t *context,
    const psx_parsed_tag_action_t *action, int is_standalone_tag,
    int *member_count, int *size, int *alignment);

static psx_qual_type_t resolve_decl_specifier_type_value(
    const decl_specifier_value_context_t *context,
    const psx_parsed_decl_specifier_t *specifier,
    psx_decl_specifier_value_status_t *status) {
  int ignored_member_count = 0;
  int ignored_size = 0;
  int ignored_alignment = 1;
  *status = resolve_tag_action_value(
      context, &specifier->tag_action, 0,
      &ignored_member_count, &ignored_size, &ignored_alignment);
  if (*status != PSX_DECL_SPECIFIER_VALUE_OK)
    return (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                             PSX_TYPE_QUALIFIER_NONE};
  psx_qual_type_t type = psx_resolve_decl_specifier_qual_type_in_context(
      context->semantic_context, specifier);
  if (type.type_id == PSX_TYPE_ID_INVALID)
    *status = PSX_DECL_SPECIFIER_VALUE_INVALID;
  return type;
}

static psx_decl_specifier_value_status_t resolve_aggregate_body_value(
    const decl_specifier_value_context_t *context,
    const psx_parsed_tag_action_t *action,
    int *member_count, int *size, int *alignment) {
  const psx_record_decl_t *record = ps_ctx_ensure_tag_record_decl_in(
      context->semantic_context, action->kind,
      action->name, action->name_len);
  if (!record || record->record_id == PSX_RECORD_ID_INVALID)
    return PSX_DECL_SPECIFIER_VALUE_INVALID;
  psx_aggregate_layout_state_t layout;
  psx_aggregate_layout_init(&layout, record);
  int resolved_member_count = 0;
  const psx_parsed_aggregate_body_t *body = action->aggregate_body;
  for (int i = 0; i < body->item_count; i++) {
    const psx_parsed_aggregate_member_declaration_t *declaration =
        &body->items[i].value.member_declaration;
    psx_decl_specifier_value_status_t base_status;
    psx_qual_type_t base_qual_type = resolve_decl_specifier_type_value(
        context, &declaration->specifier, &base_status);
    if (base_status != PSX_DECL_SPECIFIER_VALUE_OK ||
        base_qual_type.type_id == PSX_TYPE_ID_INVALID)
      return base_status;
    int requested_alignment = resolve_decl_alignment_value(
        context, &declaration->specifier);
    for (int j = 0; j < declaration->declarator_count; j++) {
      const psx_parsed_declarator_t *declarator =
          &declaration->declarators[j];
      psx_runtime_array_bound_t *bounds = NULL;
      if (declarator->array_bound_count > 0) {
        bounds = arena_alloc_in(
            ps_ctx_arena(context->semantic_context),
            (size_t)declarator->array_bound_count * sizeof(*bounds));
        if (!bounds) return PSX_DECL_SPECIFIER_VALUE_INVALID;
      }
      for (int bound = 0; bound < declarator->array_bound_count;
           bound++) {
        long long value = resolve_const_expr_value(
            context, &declarator->array_bounds[bound].expression);
        if (value > INT_MAX)
          return PSX_DECL_SPECIFIER_VALUE_INVALID;
        bounds[bound] = (psx_runtime_array_bound_t){
            .declarator_op_index =
                declarator->array_bounds[bound].declarator_op_index,
            .constant_value = value,
            .is_constant = 1,
        };
      }
      psx_runtime_declarator_application_t application;
      if (!psx_apply_resolved_runtime_parsed_declarator_in_contexts(
              context->semantic_context, context->global_registry,
              context->local_registry, declarator, bounds,
              declarator->array_bound_count, &application))
        return PSX_DECL_SPECIFIER_VALUE_INVALID;
      int bit_width = 0;
      if (declarator->has_bitfield) {
        long long value = resolve_const_expr_value(
            context, &declarator->bit_width_expression);
        bit_width = value > 0 && value <= INT_MAX ? (int)value : 0;
      }
      token_ident_t *name = declarator->identifier;
      int registered = psx_apply_aggregate_member_declaration(
          &layout,
          &(psx_aggregate_member_declaration_request_t){
              .semantic_context = context->semantic_context,
              .base_qual_type = base_qual_type,
              .declarator_shape = &application.shape,
              .member_name = name ? name->str : NULL,
              .member_name_len = name ? name->len : 0,
              .has_bitfield = declarator->has_bitfield,
              .bit_width = bit_width,
              .pack_alignment = declaration->pack_alignment,
              .requested_alignment = requested_alignment,
          },
          declarator->diagnostic_token);
      resolved_member_count += registered;
    }
  }
  *member_count = resolved_member_count;
  *size = psx_aggregate_layout_size(&layout);
  *alignment = psx_aggregate_layout_alignment(&layout);
  return PSX_DECL_SPECIFIER_VALUE_OK;
}

static psx_decl_specifier_value_status_t resolve_enum_body_value(
    const decl_specifier_value_context_t *context,
    const psx_parsed_tag_action_t *action, int *member_count) {
  long long next_value = 0;
  for (int i = 0; i < action->enum_body->member_count; i++) {
    const psx_parsed_enum_member_t *member =
        &action->enum_body->members[i];
    long long value = next_value;
    if (member->initializer &&
        !psx_resolve_enum_initializer_syntax_in_contexts(
            context->semantic_context, context->global_registry,
            context->local_registry, member->initializer,
            (token_t *)member->enumerator, &value))
      return PSX_DECL_SPECIFIER_VALUE_INVALID;
    psx_apply_parsed_enum_constant_in(
        context->semantic_context, member->enumerator->str,
        member->enumerator->len, value,
        (token_t *)member->enumerator);
    next_value = value + 1;
  }
  *member_count = action->enum_body->member_count;
  return PSX_DECL_SPECIFIER_VALUE_OK;
}

static psx_decl_specifier_value_status_t resolve_tag_action_value(
    const decl_specifier_value_context_t *context,
    const psx_parsed_tag_action_t *action, int is_standalone_tag,
    int *member_count, int *size, int *alignment) {
  *member_count = 0;
  *size = 0;
  *alignment = 1;
  if (!action || action->action == PSX_PARSED_TAG_NONE)
    return PSX_DECL_SPECIFIER_VALUE_OK;
  if (!tag_action_syntax_supported(action))
    return PSX_DECL_SPECIFIER_VALUE_NOT_SUPPORTED;
  psx_apply_parsed_tag_declaration_in(
      context->semantic_context,
      action->kind, action->name, action->name_len,
      action->action == PSX_PARSED_TAG_DEFINITION ||
              (action->action == PSX_PARSED_TAG_REFERENCE &&
               is_standalone_tag)
          ? PSX_TAG_DECLARATION_FORWARD
          : PSX_TAG_DECLARATION_REFERENCE,
      0, action->diagnostic_token);
  if (action->action != PSX_PARSED_TAG_DEFINITION)
    return PSX_DECL_SPECIFIER_VALUE_OK;
  psx_decl_specifier_value_status_t status =
      action->kind == TK_ENUM
          ? resolve_enum_body_value(context, action, member_count)
          : resolve_aggregate_body_value(
                context, action, member_count, size, alignment);
  if (status != PSX_DECL_SPECIFIER_VALUE_OK) return status;
  psx_apply_parsed_tag_declaration_in(
      context->semantic_context,
      action->kind, action->name, action->name_len,
      PSX_TAG_DECLARATION_DEFINITION, *member_count,
      action->diagnostic_token);
  if (action->kind == TK_STRUCT || action->kind == TK_UNION) {
    const psx_record_decl_t *record = ps_ctx_ensure_tag_record_decl_in(
        context->semantic_context, action->kind,
        action->name, action->name_len);
    if (!record || !ps_ctx_publish_record_layout_in(
                       context->semantic_context, record->record_id,
                       *size, *alignment))
      return PSX_DECL_SPECIFIER_VALUE_INVALID;
  }
  return PSX_DECL_SPECIFIER_VALUE_OK;
}

void psx_resolve_decl_specifier_value_in_contexts(
    const psx_decl_specifier_value_request_t *request,
    psx_decl_specifier_value_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_DECL_SPECIFIER_VALUE_INVALID;
  resolution->tag_alignment = 1;
  if (!request || !request->semantic_context ||
      !request->global_registry || !request->local_registry ||
      !request->syntax)
    return;
  if (!decl_specifier_syntax_supported(request->syntax)) {
    resolution->status = PSX_DECL_SPECIFIER_VALUE_NOT_SUPPORTED;
    return;
  }
  decl_specifier_value_context_t context = {
      .semantic_context = request->semantic_context,
      .global_registry = request->global_registry,
      .local_registry = request->local_registry,
  };
  resolution->requested_alignment = resolve_decl_alignment_value(
      &context, request->syntax);
  resolution->status = resolve_tag_action_value(
      &context, &request->syntax->tag_action,
      request->is_standalone_tag,
      &resolution->tag_member_count, &resolution->tag_size,
      &resolution->tag_alignment);
  if (resolution->status != PSX_DECL_SPECIFIER_VALUE_OK) return;
  if (request->is_standalone_tag) return;
  resolution->base_qual_type =
      psx_resolve_decl_specifier_qual_type_in_context(
          request->semantic_context, request->syntax);
  if (resolution->base_qual_type.type_id == PSX_TYPE_ID_INVALID) {
    resolution->status = PSX_DECL_SPECIFIER_VALUE_INVALID;
    return;
  }
  if (request->syntax->source == PSX_PARSED_DECL_TYPEDEF_NAME) {
    psx_typedef_info_t info;
    if (!request->syntax->typedef_name ||
        !ps_ctx_find_typedef_name_in(
            request->semantic_context,
            request->syntax->typedef_name->str,
            request->syntax->typedef_name->len, &info)) {
      resolution->status = PSX_DECL_SPECIFIER_VALUE_INVALID;
      return;
    }
    resolution->typedef_runtime_application =
        info.runtime_application;
  }
}
