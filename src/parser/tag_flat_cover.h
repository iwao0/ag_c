#ifndef PARSER_TAG_FLAT_COVER_H
#define PARSER_TAG_FLAT_COVER_H

#include "core.h"
#include "../semantic/record_decl.h"
#include "../semantic/record_layout.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;

typedef struct psx_tag_flat_cover_state_t {
  int covered_union_off;
  int covered_union_size;
} psx_tag_flat_cover_state_t;

void ps_tag_flat_cover_state_init(psx_tag_flat_cover_state_t *state);
int ps_tag_flat_cover_state_covers(const psx_tag_flat_cover_state_t *state,
                                    int member_offset);
void ps_tag_flat_cover_state_note_in(
    psx_semantic_context_t *semantic_context,
    psx_tag_flat_cover_state_t *state,
    token_kind_t tag_kind, char *tag_name, int tag_len,
    const psx_record_member_decl_t *declaration,
    const psx_record_member_layout_t *layout);

#endif
