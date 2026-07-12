#ifndef PARSER_AGGREGATE_MEMBER_DECLARATION_H
#define PARSER_AGGREGATE_MEMBER_DECLARATION_H

#include "../semantic/aggregate_member_resolution.h"
#include "core.h"

void psx_apply_resolved_aggregate_member(
    token_kind_t tag_kind, char *tag_name, int tag_name_len,
    const tag_member_info_t *member, token_t *diag_tok);
psx_aggregate_bitfield_resolution_t psx_apply_aggregate_bitfield_placement(
    psx_aggregate_layout_state_t *state, int storage_size, int bit_width,
    token_t *diag_tok);
psx_aggregate_object_placement_t psx_apply_aggregate_object_placement(
    psx_aggregate_layout_state_t *state, int storage_size,
    int natural_alignment, int pack_alignment, int requested_alignment,
    token_t *diag_tok);
void psx_validate_resolved_aggregate_member_type(
    const psx_type_t *type, token_t *diag_tok);

#endif
