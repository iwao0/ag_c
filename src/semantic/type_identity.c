#include "type_identity.h"

#include "../parser/arena.h"
#include "../parser/type_builder.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct psx_semantic_type_table_t {
  arena_context_t *arena_context;
  const psx_type_t **types;
  size_t capacity;
  psx_type_id_t next_id;
};

psx_semantic_type_table_t *psx_semantic_type_table_create(void) {
  psx_semantic_type_table_t *table = calloc(1, sizeof(*table));
  if (!table) return NULL;
  table->arena_context = arena_context_create();
  if (!table->arena_context) {
    free(table);
    return NULL;
  }
  return table;
}

void psx_semantic_type_table_destroy(psx_semantic_type_table_t *table) {
  if (!table) return;
  arena_context_destroy(table->arena_context);
  free(table->types);
  free(table);
}

void psx_semantic_type_table_reset(psx_semantic_type_table_t *table) {
  if (!table) return;
  arena_free_all_in(table->arena_context);
  free(table->types);
  table->types = NULL;
  table->capacity = 0;
  table->next_id = PSX_TYPE_ID_INVALID;
}

static int reserve_type_id(
    psx_semantic_type_table_t *table, psx_type_id_t type_id) {
  if ((size_t)type_id < table->capacity) return 1;
  size_t capacity = table->capacity ? table->capacity * 2 : 16;
  while (capacity <= (size_t)type_id) {
    if (capacity > SIZE_MAX / 2) return 0;
    capacity *= 2;
  }
  const psx_type_t **types = realloc(
      table->types, capacity * sizeof(*types));
  if (!types) return 0;
  memset(types + table->capacity, 0,
         (capacity - table->capacity) * sizeof(*types));
  table->types = types;
  table->capacity = capacity;
  return 1;
}

psx_qual_type_t psx_semantic_type_table_intern(
    psx_semantic_type_table_t *table, const psx_type_t *type) {
  psx_qual_type_t result = {PSX_TYPE_ID_INVALID,
                            PSX_TYPE_QUALIFIER_NONE};
  if (!table || !type || !ps_type_is_well_formed(type)) return result;
  result.qualifiers = ps_type_qualifiers(type);
  for (psx_type_id_t id = 1; id <= table->next_id; id++) {
    if (ps_type_unqualified_semantic_matches(table->types[id], type)) {
      result.type_id = id;
      return result;
    }
  }
  if (table->next_id == UINT_MAX) return result;
  psx_type_id_t id = table->next_id + 1;
  if (!reserve_type_id(table, id)) return result;
  psx_type_t *canonical = ps_type_clone_in(table->arena_context, type);
  if (!canonical) return result;
  ps_type_remove_qualifiers(
      canonical, PSX_TYPE_QUALIFIER_CONST | PSX_TYPE_QUALIFIER_VOLATILE |
                     PSX_TYPE_QUALIFIER_ATOMIC);
  table->types[id] = canonical;
  table->next_id = id;
  result.type_id = id;
  return result;
}

const psx_type_t *psx_semantic_type_table_lookup(
    const psx_semantic_type_table_t *table, psx_type_id_t type_id) {
  if (!table || type_id == PSX_TYPE_ID_INVALID ||
      type_id > table->next_id || (size_t)type_id >= table->capacity) {
    return NULL;
  }
  return table->types[type_id];
}
