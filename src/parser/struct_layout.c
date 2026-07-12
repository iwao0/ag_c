#include "struct_layout.h"
#include "aggregate_member_syntax.h"
#include "alignas_value.h"
#include "../semantic/declaration_application.h"
#include "diag.h"
#include "enum_const.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"

int psx_parse_tag_definition_body(token_kind_t tag_kind, char *tag_name, int tag_len,
                                  int *out_size, int *out_align) {
  if (tag_kind == TK_ENUM) {
    if (out_size) *out_size = 4;
    if (out_align) *out_align = 4;
    return psx_parse_enum_members();
  }
  return psx_parse_struct_or_union_members_layout(tag_kind, tag_name, tag_len, out_size, out_align);
}

int ps_apply_parsed_aggregate_body_layout(
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
      psx_apply_static_assert(
          item->value.static_assertion.condition,
          item->value.static_assertion.diagnostic_token);
      continue;
    }

    const psx_parsed_aggregate_member_declaration_t *declaration =
        &item->value.member_declaration;
    psx_type_t *member_base_type =
        psx_apply_parsed_decl_specifier(&declaration->specifier);
    if (!member_base_type) {
      ps_diag_ctx(declaration->specifier.diagnostic_token, "decl", "%s",
                   diag_message_for(
                       DIAG_ERR_PARSER_MEMBER_TYPE_REQUIRED));
    }
    int requested_alignment = 0;
    for (int j = 0;
         j < declaration->specifier.alignas_expression_count; j++) {
      const psx_parsed_const_expr_t *expression =
          &declaration->specifier.alignas_expressions[j];
      int value = ps_eval_parsed_alignas_value(
          expression->start, expression->end);
      if (value > requested_alignment) requested_alignment = value;
    }
    for (int j = 0; j < declaration->declarator_count; j++) {
      const psx_parsed_declarator_t *head =
          &declaration->declarators[j];
      int has_member_name = head->identifier != NULL;
      psx_declarator_shape_t resolved_shape;
      int resolved_bit_width = 0;
      psx_apply_parsed_declarator(
          head, &resolved_shape, &resolved_bit_width);
      member_count += psx_apply_aggregate_member_declaration(
          &layout,
          &(psx_aggregate_member_declaration_request_t){
              .target_tag_kind = tag_kind,
              .target_tag_name = tag_name,
              .target_tag_name_len = tag_len,
              .base_type = member_base_type,
              .declarator_shape = &resolved_shape,
              .member_name = has_member_name ? head->identifier->str : NULL,
              .member_name_len = has_member_name ? head->identifier->len : 0,
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
  ps_parse_aggregate_body(&body);
  int member_count = ps_apply_parsed_aggregate_body_layout(
      &body, tag_kind, tag_name, tag_len, out_size, out_align);
  ps_dispose_parsed_aggregate_body(&body);
  return member_count;
}
