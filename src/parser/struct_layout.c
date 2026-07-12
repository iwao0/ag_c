#include "struct_layout.h"
#include "aggregate_member_declaration.h"
#include "aggregate_member_syntax.h"
#include "alignas_value.h"
#include "diag.h"
#include "enum_const.h"
#include "semantic_ctx.h"
#include "tag_declaration.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"

static psx_type_t *resolve_member_specifier_type(
    const psx_parsed_decl_specifier_t *specifier);
static void resolve_member_declarator_syntax(
    const psx_parsed_declarator_t *parsed,
    psx_declarator_shape_t *shape, int *bit_width);

static void apply_member_tag_action(
    const psx_parsed_member_tag_action_t *action) {
  if (!action || action->action == PSX_PARSED_MEMBER_TAG_NONE) return;
  psx_apply_parsed_tag_declaration(
      action->kind, action->name, action->name_len,
      PSX_TAG_DECLARATION_REFERENCE, 0, 0, 0,
      action->diagnostic_token);
  if (action->action != PSX_PARSED_MEMBER_TAG_DEFINITION) return;

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

void psx_resolve_function_parameters_syntax(
    psx_parsed_function_parameters_t *parameters,
    psx_declarator_op_t *function_op, token_t *diagnostic_token) {
  if (!parameters || !function_op ||
      function_op->kind != PSX_DECL_OP_FUNCTION) {
    psx_diag_ctx(diagnostic_token, "declarator-syntax",
                 "invalid function parameter syntax target");
  }
  int resolved_count = 0;
  psx_ctx_enter_block_scope();
  for (int i = 0; i < parameters->count; i++) {
    psx_parsed_function_parameter_t *parameter = &parameters->items[i];
    apply_member_tag_action(&parameter->specifier.tag_action);
    psx_type_t *base = resolve_member_specifier_type(&parameter->specifier);
    if (!base) {
      psx_diag_ctx(parameter->specifier.diagnostic_token, "param", "%s",
                   diag_message_for(DIAG_ERR_PARSER_MEMBER_TYPE_REQUIRED));
    }
    psx_declarator_shape_t parameter_shape;
    int ignored_bit_width = 0;
    resolve_member_declarator_syntax(
        &parameter->declarator, &parameter_shape, &ignored_bit_width);
    psx_type_t *type = psx_type_apply_declarator_shape(base, &parameter_shape);
    if (parameters->count == 1 && type && type->kind == PSX_TYPE_VOID &&
        parameter_shape.count == 0) {
      resolved_count = 0;
      break;
    }
    if (resolved_count < 16) {
      parameters->resolved_types[resolved_count] =
          psx_type_adjust_parameter_type(type);
    }
    resolved_count++;
  }
  psx_ctx_leave_block_scope();
  function_op->has_canonical_function_params = 1;
  function_op->function_param_types = parameters->resolved_types;
  function_op->function_param_count = resolved_count;
  function_op->function_is_variadic = parameters->is_variadic;
  psx_type_t *projection = psx_type_new_function(
      psx_type_new(PSX_TYPE_VOID), (psx_decl_funcptr_sig_t){0});
  psx_type_set_function_params(
      projection, parameters->resolved_types,
      resolved_count, parameters->is_variadic);
  function_op->funcptr_sig.function.callable.signature =
      ps_type_funcptr_signature(projection).function.callable.signature;
}

static void resolve_member_declarator_syntax(
    const psx_parsed_declarator_t *parsed,
    psx_declarator_shape_t *shape, int *bit_width) {
  *shape = parsed->declarator_shape;
  for (int i = 0; i < parsed->array_bound_count; i++) {
    const psx_parsed_array_bound_t *bound =
        &parsed->array_bounds[i];
    long long value = psx_eval_parsed_enum_const_expr(
        bound->expression.start, bound->expression.end);
    if (value < 0) {
      psx_diag_ctx(bound->expression.start, "decl", "%s",
                   diag_message_for(
                       DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED));
    }
    if (value == 0) {
      psx_ctx_record_unsupported_gnu_extension_warning(
          bound->expression.start, "zero-length array");
    }
    if (bound->declarator_op_index < 0 ||
        bound->declarator_op_index >= shape->count ||
        shape->ops[bound->declarator_op_index].kind != PSX_DECL_OP_ARRAY) {
      psx_diag_ctx(bound->expression.start, "aggregate-syntax",
                   "invalid deferred array bound target");
    }
    shape->ops[bound->declarator_op_index].array_len = (int)value;
    shape->ops[bound->declarator_op_index].is_incomplete_array = 0;
  }
  for (int i = 0; i < parsed->function_suffix_count; i++) {
    const psx_parsed_function_suffix_t *suffix =
        &parsed->function_suffixes[i];
    if (suffix->declarator_op_index < 0 ||
        suffix->declarator_op_index >= shape->count ||
        shape->ops[suffix->declarator_op_index].kind !=
            PSX_DECL_OP_FUNCTION ||
        !suffix->parameters) {
      psx_diag_ctx(parsed->diagnostic_token, "aggregate-syntax",
                   "invalid deferred function suffix target");
    }
    psx_resolve_function_parameters_syntax(
        suffix->parameters,
        &shape->ops[suffix->declarator_op_index],
        parsed->diagnostic_token);
  }
  *bit_width = 0;
  if (parsed->has_bitfield) {
    long long value = psx_eval_parsed_enum_const_expr(
        parsed->bit_width_expression.start,
        parsed->bit_width_expression.end);
    *bit_width = value > 0 ? (int)value : 0;
  }
}

static psx_type_t *resolve_member_specifier_type(
    const psx_parsed_decl_specifier_t *specifier) {
  if (!specifier) return NULL;
  const psx_type_spec_result_t *syntax = &specifier->type_spec;
  psx_decl_type_request_t request = {
      .base_kind = TK_EOF,
      .fp_kind = TK_FLOAT_KIND_NONE,
      .tag_kind = TK_EOF,
      .is_unsigned = syntax->is_unsigned,
      .is_complex = syntax->is_complex,
      .is_const_qualified = syntax->is_const_qualified,
      .is_volatile_qualified = syntax->is_volatile_qualified,
      .is_atomic = syntax->is_atomic,
      .is_long_long = syntax->is_long_long,
      .is_plain_char = syntax->is_plain_char,
      .is_long_double = syntax->is_long_double,
  };
  switch (specifier->source) {
    case PSX_PARSED_DECL_TYPE_BUILTIN:
      request.base_kind = syntax->kind;
      request.override_plain_char = syntax->kind == TK_CHAR;
      break;
    case PSX_PARSED_DECL_TYPE_TAG:
      request.tag_kind = specifier->tag_action.kind;
      request.tag_name = specifier->tag_action.name;
      request.tag_len = specifier->tag_action.name_len;
      break;
    case PSX_PARSED_DECL_TYPEDEF_NAME:
      request.typedef_name = specifier->typedef_name->str;
      request.typedef_name_len = specifier->typedef_name->len;
      break;
    default:
      return NULL;
  }
  return psx_resolve_decl_type(&request);
}

int psx_parse_tag_definition_body(token_kind_t tag_kind, char *tag_name, int tag_len,
                                  int *out_size, int *out_align) {
  if (tag_kind == TK_ENUM) {
    if (out_size) *out_size = 4;
    if (out_align) *out_align = 4;
    return psx_parse_enum_members();
  }
  return psx_parse_struct_or_union_members_layout(tag_kind, tag_name, tag_len, out_size, out_align);
}

int psx_apply_parsed_aggregate_body_layout(
    const psx_parsed_aggregate_body_t *body,
    token_kind_t tag_kind, char *tag_name, int tag_len,
    int *out_size, int *out_align) {
  if (!body) return 0;
  int member_count = 0;
  psx_aggregate_layout_state_t layout;
  psx_aggregate_layout_init(&layout, tag_kind);
  for (int i = 0; i < body->item_count; i++) {
    const psx_parsed_aggregate_item_t *item = &body->items[i];
    if (item->kind == PSX_PARSED_AGGREGATE_STATIC_ASSERT) {
      psx_apply_parsed_static_assert(&item->value.static_assertion);
      continue;
    }

    const psx_parsed_aggregate_member_declaration_t *declaration =
        &item->value.member_declaration;
    apply_member_tag_action(&declaration->specifier.tag_action);
    psx_type_t *member_base_type =
        resolve_member_specifier_type(&declaration->specifier);
    if (!member_base_type) {
      psx_diag_ctx(declaration->specifier.diagnostic_token, "decl", "%s",
                   diag_message_for(
                       DIAG_ERR_PARSER_MEMBER_TYPE_REQUIRED));
    }
    int requested_alignment = 0;
    for (int j = 0;
         j < declaration->specifier.alignas_expression_count; j++) {
      const psx_parsed_const_expr_t *expression =
          &declaration->specifier.alignas_expressions[j];
      int value = psx_eval_parsed_alignas_value(
          expression->start, expression->end);
      if (value > requested_alignment) requested_alignment = value;
    }
    for (int j = 0; j < declaration->declarator_count; j++) {
      const psx_parsed_declarator_t *head =
          &declaration->declarators[j];
      int has_member_name = head->member != NULL;
      psx_declarator_shape_t resolved_shape;
      int resolved_bit_width = 0;
      resolve_member_declarator_syntax(
          head, &resolved_shape, &resolved_bit_width);
      member_count += psx_apply_aggregate_member_declaration(
          &layout,
          &(psx_aggregate_member_declaration_request_t){
              .target_tag_kind = tag_kind,
              .target_tag_name = tag_name,
              .target_tag_name_len = tag_len,
              .base_type = member_base_type,
              .declarator_shape = &resolved_shape,
              .member_name = has_member_name ? head->member->str : NULL,
              .member_name_len = has_member_name ? head->member->len : 0,
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

int psx_parse_struct_or_union_members_layout(token_kind_t tag_kind, char *tag_name, int tag_len,
                                             int *out_size, int *out_align) {
  psx_parsed_aggregate_body_t body;
  psx_parse_aggregate_body(&body);
  int member_count = psx_apply_parsed_aggregate_body_layout(
      &body, tag_kind, tag_name, tag_len, out_size, out_align);
  psx_dispose_parsed_aggregate_body(&body);
  return member_count;
}
