#include "declaration_binding_events.h"

#include "aggregate_member_syntax.h"
#include "function_parameter_syntax.h"

static void record_aggregate_body_binding_events(
    const psx_parsed_aggregate_body_t *body,
    const psx_name_classifier_t *name_classifier) {
  for (int i = 0; body && i < body->item_count; i++) {
    const psx_parsed_aggregate_item_t *item = &body->items[i];
    if (item->kind != PSX_PARSED_AGGREGATE_MEMBER_DECLARATION)
      continue;
    const psx_parsed_aggregate_member_declaration_t *declaration =
        &item->value.member_declaration;
    psx_record_decl_specifier_binding_events(
        &declaration->specifier, name_classifier);
    for (int d = 0; d < declaration->declarator_count; d++)
      psx_record_declarator_binding_events(
          &declaration->declarators[d], name_classifier);
  }
}

static void record_function_parameter_events(
    const psx_parsed_function_parameters_t *parameters,
    const psx_name_classifier_t *name_classifier,
    int declare_names);

void psx_record_decl_specifier_binding_events(
    const psx_parsed_decl_specifier_t *specifier,
    const psx_name_classifier_t *name_classifier) {
  if (!specifier ||
      specifier->binding_events_recorded ||
      specifier->source != PSX_PARSED_DECL_TYPE_TAG ||
      specifier->tag_action.action == PSX_PARSED_TAG_NONE)
    return;
  ps_name_classifier_record_binding_event(name_classifier);
  if (specifier->tag_action.kind != TK_ENUM ||
      specifier->tag_action.action != PSX_PARSED_TAG_DEFINITION ||
      !specifier->tag_action.enum_body) {
    if (specifier->tag_action.action == PSX_PARSED_TAG_DEFINITION)
      record_aggregate_body_binding_events(
          specifier->tag_action.aggregate_body, name_classifier);
    return;
  }
  for (int i = 0;
       i < specifier->tag_action.enum_body->member_count; i++)
    ps_name_classifier_record_binding_event(name_classifier);
}

void psx_record_declarator_binding_events(
    const psx_parsed_declarator_t *declarator,
    const psx_name_classifier_t *name_classifier) {
  if (!declarator) return;
  for (int i = 0; i < declarator->function_suffix_count; i++) {
    const psx_parsed_function_parameters_t *parameters =
        declarator->function_suffixes[i].parameters;
    if (!parameters) continue;
    ps_name_classifier_reserve_scope(name_classifier);
    record_function_parameter_events(
        parameters, name_classifier, 0);
  }
}

static void record_function_parameter_events(
    const psx_parsed_function_parameters_t *parameters,
    const psx_name_classifier_t *name_classifier,
    int declare_names) {
  for (int i = 0; parameters && i < parameters->count; i++) {
    const psx_parsed_function_parameter_t *parameter =
        &parameters->items[i];
    psx_record_decl_specifier_binding_events(
        &parameter->specifier, name_classifier);
    psx_record_declarator_binding_events(
        &parameter->declarator, name_classifier);
    if (!parameter->declarator.identifier) continue;
    if (declare_names) {
      ps_name_classifier_declare(
          name_classifier,
          (token_t *)parameter->declarator.identifier, 0);
    } else {
      ps_name_classifier_record_binding_event(name_classifier);
    }
  }
}

void psx_record_function_parameter_binding_events(
    const psx_parsed_function_parameters_t *parameters,
    const psx_name_classifier_t *name_classifier) {
  record_function_parameter_events(
      parameters, name_classifier, 1);
}

void psx_record_function_definition_declarator_binding_events(
    const psx_parsed_declarator_t *declarator,
    const psx_name_classifier_t *name_classifier) {
  const psx_parsed_function_suffix_t *primary =
      psx_declarator_outermost_function_suffix(declarator);
  if (!primary) return;
  for (int i = 0; i < declarator->function_suffix_count; i++) {
    const psx_parsed_function_suffix_t *suffix =
        &declarator->function_suffixes[i];
    if (suffix == primary || !suffix->parameters) continue;
    ps_name_classifier_reserve_scope(name_classifier);
    record_function_parameter_events(
        suffix->parameters, name_classifier, 0);
  }
  psx_record_function_parameter_binding_events(
      primary->parameters, name_classifier);
}
