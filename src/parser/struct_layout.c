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

static inline token_t *curtok(void) { return tk_get_current_token(); }

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

static void resolve_member_declarator_syntax(
    const psx_parsed_aggregate_member_declarator_t *parsed,
    psx_declarator_shape_t *shape, int *bit_width) {
  *shape = parsed->declarator_shape;
  for (int i = 0; i < parsed->array_bound_count; i++) {
    const psx_parsed_aggregate_array_bound_t *bound =
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
  *bit_width = 0;
  if (parsed->has_bitfield) {
    long long value = psx_eval_parsed_enum_const_expr(
        parsed->bit_width_expression.start,
        parsed->bit_width_expression.end);
    *bit_width = value > 0 ? (int)value : 0;
  }
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
    psx_decl_type_request_t type_request =
        declaration->specifier.declaration;
    if (type_request.tag_kind != TK_EOF) {
      int tag_size = psx_ctx_get_tag_size(
          type_request.tag_kind, type_request.tag_name,
          type_request.tag_len);
      if (tag_size >= 0) type_request.elem_size = tag_size;
    }
    psx_type_t *member_base_type =
        psx_resolve_decl_type(&type_request);
    if (!member_base_type) {
      psx_diag_ctx(curtok(), "member",
                   "canonical aggregate member base type resolution failed");
    }
    int requested_alignment = 0;
    for (int j = 0;
         j < declaration->specifier.alignas_expression_count; j++) {
      const psx_parsed_aggregate_const_expr_t *expression =
          &declaration->specifier.alignas_expressions[j];
      int value = psx_eval_parsed_alignas_value(
          expression->start, expression->end);
      if (value > requested_alignment) requested_alignment = value;
    }
    for (int j = 0; j < declaration->declarator_count; j++) {
      const psx_parsed_aggregate_member_declarator_t *head =
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
