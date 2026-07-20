#ifndef SEMANTIC_TYPE_COMPATIBILITY_VIEW_H
#define SEMANTIC_TYPE_COMPATIBILITY_VIEW_H

#include "../type_system/type_ids.h"

typedef struct psx_semantic_type_table_t psx_semantic_type_table_t;
typedef struct psx_type_compatibility_cache_t
    psx_type_compatibility_cache_t;
typedef struct psx_type_t psx_type_t;

/* Legacy parser-type adapter. Semantic code constructs and compares TypeShape
 * relations directly; only compatibility callers should use these imports. */
psx_qual_type_t psx_semantic_type_table_intern(
    psx_semantic_type_table_t *table, const psx_type_t *type);
psx_qual_type_t psx_semantic_type_table_find(
    const psx_semantic_type_table_t *table, const psx_type_t *type);
psx_type_compatibility_cache_t *psx_type_compatibility_cache_create(void);
void psx_type_compatibility_cache_destroy(
    psx_type_compatibility_cache_t *cache);
void psx_type_compatibility_cache_reset(
    psx_type_compatibility_cache_t *cache);
int psx_type_compatibility_cache_remember_import(
    psx_type_compatibility_cache_t *cache, psx_qual_type_t identity,
    const psx_type_t *source);
const psx_type_t *psx_type_compatibility_view(
    psx_type_compatibility_cache_t *cache,
    const psx_semantic_type_table_t *types, psx_qual_type_t type);
const psx_type_t *psx_type_compatibility_canonical_view(
    psx_type_compatibility_cache_t *cache,
    const psx_semantic_type_table_t *types, psx_type_id_t type_id);
psx_qual_type_t psx_type_compatibility_view_identity(
    const psx_type_compatibility_cache_t *cache,
    const psx_type_t *view);

#endif
