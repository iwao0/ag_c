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

/* Layout boundary for resolved semantic types. TypeId carries C type
 * identity, RecordLayoutTable carries completed aggregate placement, and all
 * ABI-dependent values come from the explicit target. */
int ps_type_layout_of_id(
    const psx_semantic_type_table_t *types,
    const psx_record_layout_table_t *record_layouts,
    psx_type_id_t type_id, const ag_target_info_t *target,
    psx_type_layout_t *out);
int ps_type_sizeof_id(
    const psx_semantic_type_table_t *types,
    const psx_record_layout_table_t *record_layouts,
    psx_type_id_t type_id, const ag_target_info_t *target);
int ps_type_alignof_id(
    const psx_semantic_type_table_t *types,
    const psx_record_layout_table_t *record_layouts,
    psx_type_id_t type_id, const ag_target_info_t *target);

#endif
