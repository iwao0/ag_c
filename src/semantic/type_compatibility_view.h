#ifndef SEMANTIC_TYPE_COMPATIBILITY_VIEW_H
#define SEMANTIC_TYPE_COMPATIBILITY_VIEW_H

#include "../type_system/type_ids.h"

typedef struct psx_semantic_type_table_t psx_semantic_type_table_t;
typedef struct psx_type_t psx_type_t;

/* Legacy parser-type adapter. Semantic code constructs and compares TypeShape
 * relations directly; only compatibility callers should use these imports. */
psx_qual_type_t psx_semantic_type_table_intern(
    psx_semantic_type_table_t *table, const psx_type_t *type);
psx_qual_type_t psx_semantic_type_table_find(
    const psx_semantic_type_table_t *table, const psx_type_t *type);
/* Immutable parser-type views for callers not yet migrated to TypeShape. */
const psx_type_t *psx_type_compatibility_canonical_view_for(
    const psx_semantic_type_table_t *types, psx_type_id_t type_id);
const psx_type_t *psx_type_compatibility_view_for(
    const psx_semantic_type_table_t *types, psx_qual_type_t type);

#endif
