#include "struct_layout.h"
#include "aggregate_member_declaration.h"
#include "aggregate_member_syntax.h"
#include "diag.h"
#include "enum_const.h"
#include "static_assert_declaration.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"
#include "../pragma_pack.h"

static inline token_t *curtok(void) { return tk_get_current_token(); }

int psx_parse_tag_definition_body(token_kind_t tag_kind, char *tag_name, int tag_len,
                                  int *out_size, int *out_align) {
  if (tag_kind == TK_ENUM) {
    if (out_size) *out_size = 4;
    if (out_align) *out_align = 4;
    return psx_parse_enum_members();
  }
  return psx_parse_struct_or_union_members_layout(tag_kind, tag_name, tag_len, out_size, out_align);
}

int psx_parse_struct_or_union_members_layout(token_kind_t tag_kind, char *tag_name, int tag_len,
                                             int *out_size, int *out_align) {
  int member_count = 0;
  psx_aggregate_layout_state_t layout;
  psx_aggregate_layout_init(&layout, tag_kind);
  while (!tk_consume('}')) {
    if (curtok()->kind == TK_STATIC_ASSERT) {
      psx_parse_static_assert_declaration();
      continue;
    }
    psx_parsed_aggregate_member_specifier_t member_specifier;
    psx_parse_aggregate_member_specifier(&member_specifier);
    token_kind_t member_tag_kind = member_specifier.declaration.tag_kind;
    char *member_tag_name = member_specifier.declaration.tag_name;
    int member_tag_len = member_specifier.declaration.tag_len;
    psx_aggregate_member_base_resolution_t member_base_resolution;
    psx_resolve_aggregate_member_base_type(
        &(psx_aggregate_member_base_resolution_request_t){
            .declaration = member_specifier.declaration,
        },
        &member_base_resolution);
    if (member_base_resolution.status != PSX_AGGREGATE_MEMBER_OK) {
      psx_diag_ctx(curtok(), "member",
                   "canonical aggregate member base type resolution failed");
    }
    for (;;) {
      psx_parsed_aggregate_member_declarator_t head =
          psx_parse_aggregate_member_declarator();
      int has_member_name = head.member != NULL;
      token_t *diag_tok = has_member_name ? (token_t *)head.member : curtok();
      member_count += psx_apply_aggregate_member_declaration(
          &layout,
          &(psx_aggregate_member_declaration_request_t){
              .target_tag_kind = tag_kind,
              .target_tag_name = tag_name,
              .target_tag_name_len = tag_len,
              .source_tag_kind = member_tag_kind,
              .source_tag_name = member_tag_name,
              .source_tag_name_len = member_tag_len,
              .base_type = member_base_resolution.type,
              .declarator_shape = &head.declarator_shape,
              .member_name = has_member_name ? head.member->str : NULL,
              .member_name_len = has_member_name ? head.member->len : 0,
              .has_bitfield = head.has_bitfield,
              .bit_width = head.bit_width,
              .is_enum_type = member_tag_kind == TK_ENUM,
              .pack_alignment = pragma_pack_current_alignment(),
              .requested_alignment = member_specifier.requested_alignment,
          },
          diag_tok);
      if (!has_member_name && !head.has_bitfield && tk_consume(','))
        psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_MEMBER_NAME));
      if (!tk_consume(',')) break;
    }
    tk_expect(';');
  }
  *out_size = psx_aggregate_layout_size(&layout);
  if (out_align) *out_align = psx_aggregate_layout_alignment(&layout);
  return member_count;
}
