#include "declaration_application.h"

#include "alignas_value.h"
#include "enum_const.h"
#include "struct_layout.h"
#include "tag_declaration.h"
#include "../semantic/declaration_resolution.h"
#include "../semantic/function_parameter_resolution.h"

static void apply_decl_tag_action(
    const psx_parsed_tag_action_t *action, void *context) {
  (void)context;
  if (!action || action->action == PSX_PARSED_TAG_NONE) return;
  psx_apply_parsed_tag_declaration(
      action->kind, action->name, action->name_len,
      PSX_TAG_DECLARATION_REFERENCE, 0, 0, 0,
      action->diagnostic_token);
  if (action->action != PSX_PARSED_TAG_DEFINITION) return;

  int member_count = 0;
  int size = 0;
  int alignment = 0;
  if (action->kind == TK_ENUM) {
    member_count = psx_apply_parsed_enum_body(action->enum_body);
    size = 4;
    alignment = 4;
  } else {
    member_count = psx_apply_parsed_aggregate_body_layout(
        action->aggregate_body, action->kind,
        action->name, action->name_len, &size, &alignment);
  }
  psx_apply_parsed_tag_declaration(
      action->kind, action->name, action->name_len,
      PSX_TAG_DECLARATION_DEFINITION, member_count, size, alignment,
      action->diagnostic_token);
}

static const psx_decl_syntax_resolution_context_t
    parser_decl_resolution_context = {
        .apply_tag_action = apply_decl_tag_action,
        .context = NULL,
};

psx_type_t *psx_apply_parsed_decl_specifier(
    const psx_parsed_decl_specifier_t *specifier) {
  return psx_resolve_decl_specifier_syntax(
      specifier, &parser_decl_resolution_context);
}

int psx_apply_parsed_decl_alignment(
    const psx_parsed_decl_specifier_t *specifier) {
  if (!specifier) return 0;
  int alignment = 0;
  for (int i = 0; i < specifier->alignas_expression_count; i++) {
    const psx_parsed_const_expr_t *expression =
        &specifier->alignas_expressions[i];
    int value = psx_eval_parsed_alignas_value(
        expression->start, expression->end);
    if (value > alignment) alignment = value;
  }
  return alignment;
}

void psx_apply_parsed_standalone_tag(
    const psx_parsed_decl_specifier_t *specifier) {
  if (!specifier || specifier->source != PSX_PARSED_DECL_TYPE_TAG) return;
  const psx_parsed_tag_action_t *action = &specifier->tag_action;
  if (action->action == PSX_PARSED_TAG_DEFINITION) {
    apply_decl_tag_action(action, NULL);
    return;
  }
  psx_apply_parsed_tag_declaration(
      action->kind, action->name, action->name_len,
      PSX_TAG_DECLARATION_FORWARD, 0, 0, 0,
      action->diagnostic_token);
}

void psx_apply_parsed_declarator(
    const psx_parsed_declarator_t *declarator,
    psx_declarator_shape_t *shape, int *bit_width) {
  psx_resolve_declarator_syntax(
      declarator, shape, bit_width, &parser_decl_resolution_context);
}

void psx_apply_parsed_function_parameters(
    psx_parsed_function_parameters_t *parameters,
    psx_declarator_op_t *function_op, token_t *diagnostic_token) {
  psx_resolve_function_parameter_types(
      parameters, function_op, diagnostic_token,
      &parser_decl_resolution_context);
}
