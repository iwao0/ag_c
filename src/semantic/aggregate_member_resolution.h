#ifndef SEMANTIC_AGGREGATE_MEMBER_RESOLUTION_H
#define SEMANTIC_AGGREGATE_MEMBER_RESOLUTION_H

#include "declaration_resolution.h"
#include "../parser/declarator_shape.h"
#include "../parser/tag_member_public.h"

typedef enum {
  PSX_AGGREGATE_MEMBER_OK = 0,
  PSX_AGGREGATE_MEMBER_INVALID,
  PSX_AGGREGATE_MEMBER_DUPLICATE,
  PSX_AGGREGATE_MEMBER_BIT_WIDTH_EXCEEDS_STORAGE,
  PSX_AGGREGATE_MEMBER_INCOMPLETE_TYPE,
  PSX_AGGREGATE_MEMBER_FUNCTION_TYPE,
  PSX_AGGREGATE_MEMBER_INVALID_BITFIELD_TYPE,
  PSX_AGGREGATE_MEMBER_MISSING_NAME,
} psx_aggregate_member_status_t;

typedef struct {
  token_kind_t kind;
  int current_offset;
  int union_size;
  int alignment;
  int bitfield_storage_offset;
  int bitfield_storage_size;
  int bitfield_bits_used;
} psx_aggregate_layout_state_t;

typedef struct {
  psx_semantic_context_t *semantic_context;
  token_kind_t target_tag_kind;
  char *target_tag_name;
  int target_tag_name_len;
  const psx_type_t *base_type;
  const psx_declarator_shape_t *declarator_shape;
  char *member_name;
  int member_name_len;
  int has_bitfield;
  int bit_width;
  int pack_alignment;
  int requested_alignment;
} psx_aggregate_member_declaration_request_t;

typedef struct {
  psx_aggregate_member_status_t status;
  const psx_type_t *type;
  int offset;
  int storage_size;
  int bit_offset;
  int bit_is_signed;
  int registered_member_count;
  char *conflicting_name;
  int conflicting_name_len;
} psx_aggregate_member_declaration_resolution_t;

void psx_aggregate_layout_init(
    psx_aggregate_layout_state_t *state, token_kind_t kind);
int psx_aggregate_layout_size(const psx_aggregate_layout_state_t *state);
int psx_aggregate_layout_alignment(const psx_aggregate_layout_state_t *state);
void psx_resolve_aggregate_member_declaration(
    psx_aggregate_layout_state_t *layout,
    const psx_aggregate_member_declaration_request_t *request,
    psx_aggregate_member_declaration_resolution_t *resolution);

#endif
