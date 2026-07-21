#ifndef SEMANTIC_TYPE_COMPATIBILITY_CACHE_STORAGE_INTERNAL_H
#define SEMANTIC_TYPE_COMPATIBILITY_CACHE_STORAGE_INTERNAL_H

#include "type_compatibility_cache_internal.h"
#include "../type_system/type_ids.h"
#include <stddef.h>

typedef struct arena_context_t arena_context_t;

enum { PSX_QUALIFIER_VIEW_COUNT = 16 };

typedef struct {
  const void *canonical_view;
  const void *views[PSX_QUALIFIER_VIEW_COUNT];
  int record_tag_scope_depth_p1;
  unsigned char canonical_materializing;
  unsigned char materializing[PSX_QUALIFIER_VIEW_COUNT];
} psx_type_compatibility_entry_t;

struct psx_type_compatibility_cache_t {
  arena_context_t *arena_context;
  psx_type_compatibility_entry_t *entries;
  size_t capacity;
};

int psx_type_compatibility_cache_reserve_type_id(
    psx_type_compatibility_cache_t *cache, psx_type_id_t type_id);

#endif
