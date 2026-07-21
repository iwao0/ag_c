#include "type_compatibility_cache_storage_internal.h"

#include "../parser/arena.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

psx_type_compatibility_cache_t *psx_type_compatibility_cache_create(void) {
  psx_type_compatibility_cache_t *cache = calloc(1, sizeof(*cache));
  if (!cache) return NULL;
  cache->arena_context = arena_context_create();
  if (!cache->arena_context) {
    free(cache);
    return NULL;
  }
  return cache;
}

void psx_type_compatibility_cache_destroy(
    psx_type_compatibility_cache_t *cache) {
  if (!cache) return;
  arena_context_destroy(cache->arena_context);
  free(cache->entries);
  free(cache);
}

void psx_type_compatibility_cache_reset(
    psx_type_compatibility_cache_t *cache) {
  if (!cache) return;
  arena_free_all_in(cache->arena_context);
  free(cache->entries);
  cache->entries = NULL;
  cache->capacity = 0;
}

int psx_type_compatibility_cache_reserve_type_id(
    psx_type_compatibility_cache_t *cache, psx_type_id_t type_id) {
  if ((size_t)type_id < cache->capacity) return 1;
  size_t capacity = cache->capacity ? cache->capacity * 2 : 16;
  while (capacity <= (size_t)type_id) {
    if (capacity > SIZE_MAX / 2) return 0;
    capacity *= 2;
  }
  psx_type_compatibility_entry_t *entries = realloc(
      cache->entries, capacity * sizeof(*entries));
  if (!entries) return 0;
  memset(entries + cache->capacity, 0,
         (capacity - cache->capacity) * sizeof(*entries));
  cache->entries = entries;
  cache->capacity = capacity;
  return 1;
}
