#ifndef SEMANTIC_TYPE_IDENTITY_H
#define SEMANTIC_TYPE_IDENTITY_H

#include "../parser/type.h"

typedef unsigned int psx_type_id_t;
typedef struct psx_semantic_type_table_t psx_semantic_type_table_t;

#define PSX_TYPE_ID_INVALID ((psx_type_id_t)0)

typedef struct {
  psx_type_id_t type_id;
  psx_type_qualifiers_t qualifiers;
} psx_qual_type_t;

psx_semantic_type_table_t *psx_semantic_type_table_create(void);
void psx_semantic_type_table_destroy(psx_semantic_type_table_t *table);
void psx_semantic_type_table_reset(psx_semantic_type_table_t *table);
psx_qual_type_t psx_semantic_type_table_intern(
    psx_semantic_type_table_t *table, const psx_type_t *type);
const psx_type_t *psx_semantic_type_table_lookup(
    const psx_semantic_type_table_t *table, psx_type_id_t type_id);

#endif
