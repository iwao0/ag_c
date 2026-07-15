#ifndef AG_TYPE_LAYOUT_H
#define AG_TYPE_LAYOUT_H

#include "target_info.h"
#include "semantic/type_identity.h"

typedef struct psx_type_t psx_type_t;

typedef struct {
  int size;
  int alignment;
  unsigned char is_complete;
} psx_type_layout_t;

/* Computes object layout from semantic type identity and an explicit target.
 * Target-dependent code must use this API instead of psx_type_t::size/align. */
int ps_type_layout_of(
    const psx_type_t *type, const ag_target_info_t *target,
    psx_type_layout_t *out);
int ps_type_sizeof_for_target(
    const psx_type_t *type, const ag_target_info_t *target);
int ps_type_alignof_for_target(
    const psx_type_t *type, const ag_target_info_t *target);

/* Layout boundary for resolved semantic types. The TypeId carries C type
 * identity; all ABI-dependent values come from the explicit target. */
int ps_type_layout_of_id(
    const psx_semantic_type_table_t *types, psx_type_id_t type_id,
    const ag_target_info_t *target, psx_type_layout_t *out);
int ps_type_sizeof_id_for_target(
    const psx_semantic_type_table_t *types, psx_type_id_t type_id,
    const ag_target_info_t *target);
int ps_type_alignof_id_for_target(
    const psx_semantic_type_table_t *types, psx_type_id_t type_id,
    const ag_target_info_t *target);

#endif
