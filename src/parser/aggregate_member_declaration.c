#include "aggregate_member_declaration.h"

#include "diag.h"
#include "../diag/diag.h"

void psx_validate_resolved_aggregate_member_type(
    const psx_type_t *type, token_t *diag_tok) {
  psx_aggregate_member_status_t status =
      psx_validate_aggregate_member_type(type);
  if (status == PSX_AGGREGATE_MEMBER_OK) return;
  if (status == PSX_AGGREGATE_MEMBER_INCOMPLETE_TYPE) {
    psx_diag_ctx(diag_tok, "decl", "%s",
                 diag_message_for(
                     DIAG_ERR_PARSER_INCOMPLETE_MEMBER_FORBIDDEN));
  }
  if (status == PSX_AGGREGATE_MEMBER_FUNCTION_TYPE) {
    psx_diag_ctx(diag_tok, "decl", "%s",
                 diag_message_for(
                     DIAG_ERR_PARSER_FUNCTION_MEMBER_FORBIDDEN));
  }
  psx_diag_ctx(diag_tok, "member",
               "canonical aggregate member type validation failed");
}

psx_aggregate_bitfield_resolution_t psx_apply_aggregate_bitfield_placement(
    psx_aggregate_layout_state_t *state, int storage_size, int bit_width,
    token_t *diag_tok) {
  psx_aggregate_bitfield_resolution_t resolution;
  psx_resolve_aggregate_bitfield_placement(
      state,
      &(psx_aggregate_bitfield_request_t){
          .storage_size = storage_size,
          .bit_width = bit_width,
      },
      &resolution);
  if (resolution.status == PSX_AGGREGATE_MEMBER_OK) return resolution;
  if (resolution.status == PSX_AGGREGATE_MEMBER_BIT_WIDTH_EXCEEDS_STORAGE) {
    psx_diag_ctx(diag_tok, "member",
                 "bit-field width %d exceeds its %d-bit storage type",
                 bit_width, storage_size * 8);
  }
  psx_diag_ctx(diag_tok, "member",
               "aggregate bit-field placement resolution failed");
  return resolution;
}

psx_aggregate_object_placement_t psx_apply_aggregate_object_placement(
    psx_aggregate_layout_state_t *state, int storage_size,
    int natural_alignment, int pack_alignment, int requested_alignment,
    token_t *diag_tok) {
  psx_aggregate_object_placement_t placement;
  psx_resolve_aggregate_object_placement(
      state,
      &(psx_aggregate_object_placement_request_t){
          .storage_size = storage_size,
          .natural_alignment = natural_alignment,
          .pack_alignment = pack_alignment,
          .requested_alignment = requested_alignment,
      },
      &placement);
  if (placement.status == PSX_AGGREGATE_MEMBER_OK) return placement;
  psx_diag_ctx(diag_tok, "member",
               "aggregate object placement resolution failed");
  return placement;
}

void psx_apply_resolved_aggregate_member(
    token_kind_t tag_kind, char *tag_name, int tag_name_len,
    const tag_member_info_t *member, token_t *diag_tok) {
  psx_aggregate_member_resolution_t resolution;
  psx_resolve_aggregate_member(
      &(psx_aggregate_member_resolution_request_t){
          .tag_kind = tag_kind,
          .tag_name = tag_name,
          .tag_name_len = tag_name_len,
          .member = member,
      },
      &resolution);
  if (resolution.status == PSX_AGGREGATE_MEMBER_OK) return;
  if (resolution.status == PSX_AGGREGATE_MEMBER_DUPLICATE) {
    psx_diag_ctx(diag_tok, "member",
                 "メンバ '%.*s' は同じaggregate内で重複しています (C11 6.7.2.1)",
                 member ? member->len : 0,
                 member && member->name ? member->name : "");
  }
  psx_diag_ctx(diag_tok, "member",
               "canonical aggregate member resolution failed");
}
