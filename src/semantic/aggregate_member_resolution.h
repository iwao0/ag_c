#ifndef SEMANTIC_AGGREGATE_MEMBER_RESOLUTION_H
#define SEMANTIC_AGGREGATE_MEMBER_RESOLUTION_H

#include "declaration_resolution.h"
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
  const psx_type_t *type;
  int is_enum_type;
  int bit_width;
} psx_aggregate_bitfield_request_t;

typedef struct {
  psx_aggregate_member_status_t status;
  int offset;
  int bit_offset;
  int storage_size;
  int bit_is_signed;
} psx_aggregate_bitfield_resolution_t;

typedef struct {
  int storage_size;
  int natural_alignment;
  int pack_alignment;
  int requested_alignment;
} psx_aggregate_object_placement_request_t;

typedef struct {
  psx_aggregate_member_status_t status;
  int offset;
  int alignment;
} psx_aggregate_object_placement_t;

typedef struct {
  token_kind_t tag_kind;
  char *tag_name;
  int tag_name_len;
  char *member_name;
  int member_name_len;
  int offset;
  psx_type_t *type;
  int bit_width;
  int bit_offset;
  int bit_is_signed;
} psx_aggregate_member_resolution_request_t;

typedef struct {
  psx_aggregate_member_status_t status;
  int created;
  int scope_depth;
} psx_aggregate_member_resolution_t;

typedef struct {
  token_kind_t target_tag_kind;
  char *target_tag_name;
  int target_tag_name_len;
  token_kind_t source_tag_kind;
  char *source_tag_name;
  int source_tag_name_len;
  int base_offset;
} psx_aggregate_member_promotion_request_t;

typedef struct {
  psx_aggregate_member_status_t status;
  int promoted_count;
  char *conflicting_name;
  int conflicting_name_len;
} psx_aggregate_member_promotion_t;

typedef struct {
  psx_decl_type_request_t declaration;
} psx_aggregate_member_type_request_t;

typedef struct {
  psx_aggregate_member_status_t status;
  int storage_size;
  int alignment;
  int value_size;
  int scalar_size;
  int deref_size;
  int array_element_count;
  int array_dim_count;
  int array_dims[8];
  int pointee_array_size;
  int pointee_array_element_size;
  int pointee_array_dim_count;
  int pointee_array_dims[8];
  int is_pointer_object;
  int pointer_depth;
  int is_flexible_array;
  int is_unsigned;
  tk_float_kind_t fp_kind;
  token_kind_t scalar_kind;
  int is_bool;
  int is_complex;
  int is_atomic;
} psx_aggregate_member_storage_plan_t;

typedef struct {
  psx_decl_type_request_t declaration;
} psx_aggregate_member_base_resolution_request_t;

typedef struct {
  psx_aggregate_member_status_t status;
  psx_type_t *type;
  psx_aggregate_member_storage_plan_t storage;
} psx_aggregate_member_base_resolution_t;

typedef struct {
  token_kind_t target_tag_kind;
  char *target_tag_name;
  int target_tag_name_len;
  token_kind_t source_tag_kind;
  char *source_tag_name;
  int source_tag_name_len;
  const psx_type_t *base_type;
  const psx_declarator_shape_t *declarator_shape;
  char *member_name;
  int member_name_len;
  int has_bitfield;
  int bit_width;
  int is_enum_type;
  int pack_alignment;
  int requested_alignment;
} psx_aggregate_member_declaration_request_t;

typedef struct {
  psx_aggregate_member_status_t status;
  psx_type_t *type;
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
void psx_resolve_aggregate_bitfield_placement(
    psx_aggregate_layout_state_t *state,
    const psx_aggregate_bitfield_request_t *request,
    psx_aggregate_bitfield_resolution_t *resolution);
void psx_resolve_aggregate_object_placement(
    psx_aggregate_layout_state_t *state,
    const psx_aggregate_object_placement_request_t *request,
    psx_aggregate_object_placement_t *placement);
int psx_aggregate_layout_size(const psx_aggregate_layout_state_t *state);
int psx_aggregate_layout_alignment(const psx_aggregate_layout_state_t *state);
void psx_resolve_aggregate_member(
    const psx_aggregate_member_resolution_request_t *request,
    psx_aggregate_member_resolution_t *resolution);
void psx_promote_aggregate_members(
    const psx_aggregate_member_promotion_request_t *request,
    psx_aggregate_member_promotion_t *promotion);
psx_type_t *psx_resolve_aggregate_member_type(
    const psx_aggregate_member_type_request_t *request);
void psx_plan_aggregate_member_storage(
    const psx_type_t *type, psx_aggregate_member_storage_plan_t *plan);
psx_aggregate_member_status_t psx_validate_aggregate_member_type(
    const psx_type_t *type);
void psx_resolve_aggregate_member_base_type(
    const psx_aggregate_member_base_resolution_request_t *request,
    psx_aggregate_member_base_resolution_t *resolution);
void psx_resolve_aggregate_member_declaration(
    psx_aggregate_layout_state_t *layout,
    const psx_aggregate_member_declaration_request_t *request,
    psx_aggregate_member_declaration_resolution_t *resolution);

#endif
