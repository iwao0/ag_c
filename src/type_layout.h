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
 * object-representation values come from the explicit DataLayout. Calling
 * convention policy is intentionally unavailable at this boundary. */
int psx_type_layout_of(const psx_semantic_type_table_t *types,
                       const psx_record_layout_table_t *record_layouts,
                       psx_type_id_t type_id,
                       const ag_data_layout_t *data_layout,
                       psx_type_layout_t *out);
int psx_type_layout_sizeof(const psx_semantic_type_table_t *types,
                           const psx_record_layout_table_t *record_layouts,
                           psx_type_id_t type_id,
                           const ag_data_layout_t *data_layout);
int psx_type_layout_alignof(const psx_semantic_type_table_t *types,
                            const psx_record_layout_table_t *record_layouts,
                            psx_type_id_t type_id,
                            const ag_data_layout_t *data_layout);
int psx_type_layout_character_code_unit_width(
    const psx_semantic_type_table_t *types, psx_type_id_t type_id,
    const ag_data_layout_t *data_layout);

#endif
