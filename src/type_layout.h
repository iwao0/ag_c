#ifndef AG_TYPE_LAYOUT_H
#define AG_TYPE_LAYOUT_H

#include "target_info.h"
#include "semantic/type_identity.h"
#include "semantic/record_layout.h"

typedef struct {
  int size;
  int alignment;
  unsigned char is_complete;
} psx_type_layout_t;

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

/* Record layout is a separate input from C type identity. These APIs never
 * read aggregate size/alignment cached in parser-owned declarations. */
int ps_type_layout_of_id_with_records(
    const psx_semantic_type_table_t *types,
    const psx_record_layout_table_t *record_layouts,
    psx_type_id_t type_id, const ag_target_info_t *target,
    psx_type_layout_t *out);
int ps_type_sizeof_id_with_records(
    const psx_semantic_type_table_t *types,
    const psx_record_layout_table_t *record_layouts,
    psx_type_id_t type_id, const ag_target_info_t *target);
int ps_type_alignof_id_with_records(
    const psx_semantic_type_table_t *types,
    const psx_record_layout_table_t *record_layouts,
    psx_type_id_t type_id, const ag_target_info_t *target);

#endif
