#ifndef SEMANTIC_RECORD_DECL_TABLE_H
#define SEMANTIC_RECORD_DECL_TABLE_H

#include "record_decl.h"

typedef struct psx_record_decl_table_t psx_record_decl_table_t;

psx_record_decl_table_t *psx_record_decl_table_create(void);
void psx_record_decl_table_destroy(psx_record_decl_table_t *table);
void psx_record_decl_table_reset(psx_record_decl_table_t *table);

int psx_record_decl_table_define(
    psx_record_decl_table_t *table, const psx_record_decl_t *record);
const psx_record_decl_t *psx_record_decl_table_lookup(
    const psx_record_decl_table_t *table, psx_record_id_t record_id);

#endif
