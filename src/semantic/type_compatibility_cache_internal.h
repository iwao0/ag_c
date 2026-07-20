#ifndef SEMANTIC_TYPE_COMPATIBILITY_CACHE_INTERNAL_H
#define SEMANTIC_TYPE_COMPATIBILITY_CACHE_INTERNAL_H

typedef struct psx_type_compatibility_cache_t
    psx_type_compatibility_cache_t;

psx_type_compatibility_cache_t *psx_type_compatibility_cache_create(void);
void psx_type_compatibility_cache_destroy(
    psx_type_compatibility_cache_t *cache);
void psx_type_compatibility_cache_reset(
    psx_type_compatibility_cache_t *cache);

#endif
