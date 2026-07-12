#include "aggregate_member_declaration.h"

#include "diag.h"
#include "../diag/diag.h"

int psx_apply_aggregate_member_declaration(
    psx_aggregate_layout_state_t *layout,
    const psx_aggregate_member_declaration_request_t *request,
    token_t *diag_tok) {
  psx_aggregate_member_declaration_resolution_t resolution;
  psx_resolve_aggregate_member_declaration(layout, request, &resolution);
  if (resolution.status == PSX_AGGREGATE_MEMBER_OK)
    return resolution.registered_member_count;
  if (resolution.status == PSX_AGGREGATE_MEMBER_MISSING_NAME) {
    psx_diag_missing(diag_tok, diag_text_for(DIAG_TEXT_MEMBER_NAME));
  }
  if (resolution.status == PSX_AGGREGATE_MEMBER_INCOMPLETE_TYPE) {
    psx_diag_ctx(diag_tok, "decl", "%s",
                 diag_message_for(
                     DIAG_ERR_PARSER_INCOMPLETE_MEMBER_FORBIDDEN));
  }
  if (resolution.status == PSX_AGGREGATE_MEMBER_FUNCTION_TYPE) {
    psx_diag_ctx(diag_tok, "decl", "%s",
                 diag_message_for(
                     DIAG_ERR_PARSER_FUNCTION_MEMBER_FORBIDDEN));
  }
  if (resolution.status ==
      PSX_AGGREGATE_MEMBER_BIT_WIDTH_EXCEEDS_STORAGE) {
    psx_diag_ctx(diag_tok, "member",
                 "bit-field width %d exceeds its %d-bit storage type",
                 request ? request->bit_width : 0,
                 resolution.storage_size * 8);
  }
  if (resolution.status == PSX_AGGREGATE_MEMBER_INVALID_BITFIELD_TYPE) {
    psx_diag_ctx(diag_tok, "member",
                 "bit-field has non-integer canonical type");
  }
  if (resolution.status == PSX_AGGREGATE_MEMBER_DUPLICATE) {
    psx_diag_ctx(
        diag_tok, "member",
        "メンバ '%.*s' は同じaggregate内で重複しています (C11 6.7.2.1)",
        resolution.conflicting_name_len,
        resolution.conflicting_name ? resolution.conflicting_name : "");
  }
  psx_diag_ctx(diag_tok, "member",
               "aggregate member declaration resolution failed");
  return 0;
}
