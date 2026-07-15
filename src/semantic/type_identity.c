#include "type_identity.h"

#include "../parser/arena.h"
#include "../parser/tag_member_public.h"
#include "../parser/type_builder.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const psx_type_t *type;
  psx_type_id_t base_type_id;
  psx_type_id_t *parameter_type_ids;
  int parameter_count;
  psx_type_id_t *record_member_type_ids;
  int record_member_count;
  unsigned char relations_populating;
} psx_semantic_type_entry_t;

struct psx_semantic_type_table_t {
  arena_context_t *arena_context;
  psx_semantic_type_entry_t *entries;
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
  free(table->entries);
  free(table);
}

void psx_semantic_type_table_reset(psx_semantic_type_table_t *table) {
  if (!table) return;
  arena_free_all_in(table->arena_context);
  free(table->entries);
  table->entries = NULL;
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
  psx_semantic_type_entry_t *entries = realloc(
      table->entries, capacity * sizeof(*entries));
  if (!entries) return 0;
  memset(entries + table->capacity, 0,
         (capacity - table->capacity) * sizeof(*entries));
  table->entries = entries;
  table->capacity = capacity;
  return 1;
}

static psx_qual_type_t invalid_qual_type(void) {
  return (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                           PSX_TYPE_QUALIFIER_NONE};
}

psx_qual_type_t psx_semantic_type_table_find(
    const psx_semantic_type_table_t *table, const psx_type_t *type) {
  psx_qual_type_t result = {PSX_TYPE_ID_INVALID,
                            PSX_TYPE_QUALIFIER_NONE};
  if (!table || !type || type->kind == PSX_TYPE_INVALID ||
      !ps_type_is_well_formed(type)) {
    return result;
  }
  result.qualifiers = ps_type_qualifiers(type);
  for (psx_type_id_t id = 1; id <= table->next_id; id++) {
    if (ps_type_unqualified_semantic_matches(
            table->entries[id].type, type)) {
      result.type_id = id;
      return result;
    }
  }
  return invalid_qual_type();
}

static int populate_type_relations_body(
    psx_semantic_type_table_t *table, psx_type_id_t id,
    const psx_type_t *type) {
  if (!table || id == PSX_TYPE_ID_INVALID || !type) return 0;
  if (type->base) {
    psx_qual_type_t base = psx_semantic_type_table_intern(
        table, type->base);
    if (base.type_id == PSX_TYPE_ID_INVALID) return 0;
    table->entries[id].base_type_id = base.type_id;
  }
  if (type->param_count > table->entries[id].parameter_count) {
    psx_type_id_t *parameter_type_ids = arena_alloc_in(
        table->arena_context,
        (size_t)type->param_count * sizeof(*parameter_type_ids));
    if (!parameter_type_ids) return 0;
    table->entries[id].parameter_type_ids = parameter_type_ids;
    table->entries[id].parameter_count = type->param_count;
  }
  for (int i = 0; i < type->param_count; i++) {
    if (!type->param_types) return 0;
    psx_qual_type_t parameter = psx_semantic_type_table_intern(
        table, type->param_types[i]);
    if (parameter.type_id == PSX_TYPE_ID_INVALID) return 0;
    table->entries[id].parameter_type_ids[i] = parameter.type_id;
  }
  const psx_aggregate_definition_t *definition =
      type->aggregate_definition;
  if (definition &&
      definition->member_count > table->entries[id].record_member_count) {
    psx_type_id_t *record_member_type_ids = arena_alloc_in(
        table->arena_context,
        (size_t)definition->member_count * sizeof(*record_member_type_ids));
    if (!record_member_type_ids) return 0;
    table->entries[id].record_member_type_ids = record_member_type_ids;
    table->entries[id].record_member_count = definition->member_count;
  }
  for (int i = 0; definition && i < definition->member_count; i++) {
    const psx_type_t *member_type =
        ps_tag_member_decl_type(&definition->members[i]);
    if (!member_type) continue;
    psx_qual_type_t member = psx_semantic_type_table_intern(
        table, member_type);
    if (member.type_id == PSX_TYPE_ID_INVALID) return 0;
    table->entries[id].record_member_type_ids[i] = member.type_id;
  }
  return 1;
}

static int populate_type_relations(
    psx_semantic_type_table_t *table, psx_type_id_t id,
    const psx_type_t *type) {
  if (!table || id == PSX_TYPE_ID_INVALID || !type) return 0;
  if (table->entries[id].relations_populating) return 1;
  table->entries[id].relations_populating = 1;
  int result = populate_type_relations_body(table, id, type);
  table->entries[id].relations_populating = 0;
  return result;
}

psx_qual_type_t psx_semantic_type_table_intern(
    psx_semantic_type_table_t *table, const psx_type_t *type) {
  if (!table || !type || type->kind == PSX_TYPE_INVALID ||
      !ps_type_is_well_formed(type)) {
    return invalid_qual_type();
  }
  psx_qual_type_t result = psx_semantic_type_table_find(table, type);
  if (result.type_id != PSX_TYPE_ID_INVALID) {
    return populate_type_relations(table, result.type_id, type)
               ? result
               : invalid_qual_type();
  }
  result.qualifiers = ps_type_qualifiers(type);
  if (table->next_id == UINT_MAX) return result;
  psx_type_id_t id = table->next_id + 1;
  if (!reserve_type_id(table, id)) return result;
  psx_type_t *canonical = ps_type_clone_in(table->arena_context, type);
  if (!canonical) return result;
  ps_type_remove_qualifiers(
      canonical, PSX_TYPE_QUALIFIER_CONST | PSX_TYPE_QUALIFIER_VOLATILE |
                     PSX_TYPE_QUALIFIER_ATOMIC);
  table->entries[id].type = canonical;
  table->next_id = id;
  result.type_id = id;
  return populate_type_relations(table, id, type)
             ? result
             : invalid_qual_type();
}

const psx_type_t *psx_semantic_type_table_lookup(
    const psx_semantic_type_table_t *table, psx_type_id_t type_id) {
  if (!table || type_id == PSX_TYPE_ID_INVALID ||
      type_id > table->next_id || (size_t)type_id >= table->capacity) {
    return NULL;
  }
  return table->entries[type_id].type;
}

static psx_qual_type_t related_type(
    const psx_semantic_type_table_t *table, psx_type_id_t related_type_id,
    const psx_type_t *qualified_type) {
  if (!table || related_type_id == PSX_TYPE_ID_INVALID ||
      !psx_semantic_type_table_lookup(table, related_type_id))
    return invalid_qual_type();
  return (psx_qual_type_t){
      .type_id = related_type_id,
      .qualifiers = ps_type_qualifiers(qualified_type),
  };
}

psx_qual_type_t psx_semantic_type_table_base(
    const psx_semantic_type_table_t *table, psx_type_id_t type_id) {
  const psx_type_t *type = psx_semantic_type_table_lookup(table, type_id);
  if (!type) return invalid_qual_type();
  return related_type(
      table, table->entries[type_id].base_type_id, type->base);
}

psx_qual_type_t psx_semantic_type_table_array_leaf(
    const psx_semantic_type_table_t *table, psx_type_id_t type_id) {
  psx_qual_type_t result = related_type(
      table, type_id, psx_semantic_type_table_lookup(table, type_id));
  const psx_type_t *type = psx_semantic_type_table_lookup(
      table, result.type_id);
  while (type && type->kind == PSX_TYPE_ARRAY) {
    result = psx_semantic_type_table_base(table, result.type_id);
    type = psx_semantic_type_table_lookup(table, result.type_id);
  }
  return type ? result : invalid_qual_type();
}

psx_qual_type_t psx_semantic_type_table_pointee_value(
    const psx_semantic_type_table_t *table, psx_type_id_t type_id) {
  const psx_type_t *type = psx_semantic_type_table_lookup(table, type_id);
  if (!type || (type->kind != PSX_TYPE_POINTER &&
                type->kind != PSX_TYPE_ARRAY)) {
    return invalid_qual_type();
  }
  psx_qual_type_t base = psx_semantic_type_table_base(table, type_id);
  return psx_semantic_type_table_array_leaf(table, base.type_id);
}

psx_qual_type_t psx_semantic_type_table_parameter(
    const psx_semantic_type_table_t *table, psx_type_id_t type_id,
    int parameter_index) {
  const psx_type_t *type = psx_semantic_type_table_lookup(table, type_id);
  if (!type || parameter_index < 0 ||
      parameter_index >= table->entries[type_id].parameter_count ||
      !type->param_types)
    return invalid_qual_type();
  return related_type(
      table, table->entries[type_id].parameter_type_ids[parameter_index],
      type->param_types[parameter_index]);
}

psx_qual_type_t psx_semantic_type_table_record_member(
    const psx_semantic_type_table_t *table, psx_type_id_t type_id,
    int member_index) {
  const psx_type_t *type = psx_semantic_type_table_lookup(table, type_id);
  const psx_aggregate_definition_t *definition =
      type ? type->aggregate_definition : NULL;
  if (!definition || member_index < 0 ||
      member_index >= table->entries[type_id].record_member_count)
    return invalid_qual_type();
  return related_type(
      table, table->entries[type_id].record_member_type_ids[member_index],
      ps_tag_member_decl_type(&definition->members[member_index]));
}
