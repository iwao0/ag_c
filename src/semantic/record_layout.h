#ifndef SEMANTIC_RECORD_LAYOUT_H
#define SEMANTIC_RECORD_LAYOUT_H

#include "../parser/type.h"
#include "../target_info.h"

typedef struct psx_record_layout_table_t psx_record_layout_table_t;

typedef struct {
  int offset;
  int bit_offset;
} psx_record_member_layout_t;

typedef struct {
  psx_record_id_t record_id;
  ag_data_layout_t data_layout;
  int size;
  int alignment;
  int member_count;
  const psx_record_member_layout_t *members;
} psx_record_layout_t;

psx_record_layout_table_t *psx_record_layout_table_create(void);
void psx_record_layout_table_destroy(psx_record_layout_table_t *table);
void psx_record_layout_table_reset(psx_record_layout_table_t *table);

int psx_record_layout_table_define(
    psx_record_layout_table_t *table, psx_record_id_t record_id,
    const ag_target_info_t *target, int size, int alignment,
    const psx_record_member_layout_t *members, int member_count);
const psx_record_layout_t *psx_record_layout_table_lookup(
    const psx_record_layout_table_t *table, psx_record_id_t record_id,
    const ag_target_info_t *target);
const psx_record_member_layout_t *psx_record_layout_member(
    const psx_record_layout_t *layout, int member_index);

#endif
