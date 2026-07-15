#include "type_identity.h"

#include "../parser/arena.h"
#include "../parser/type_builder.h"
#include "record_decl_table.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  psx_type_t *type;
  psx_qual_type_t base_type;
  psx_qual_type_t *parameter_types;
  int parameter_count;
  psx_qual_type_t *record_member_types;
  int record_member_count;
  unsigned char relations_populating;
  unsigned char canonical_relations_materialized;
} psx_semantic_type_entry_t;

struct psx_semantic_type_table_t {
  arena_context_t *arena_context;
  const psx_record_decl_table_t *record_decls;
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

void psx_semantic_type_table_bind_record_decls(
    psx_semantic_type_table_t *table,
    const psx_record_decl_table_t *record_decls) {
  if (table) table->record_decls = record_decls;
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

static psx_type_id_t canonical_type_id(
    const psx_semantic_type_table_t *table, const psx_type_t *type) {
  if (!table || !type) return PSX_TYPE_ID_INVALID;
  for (psx_type_id_t id = 1; id <= table->next_id; id++) {
    if ((size_t)id < table->capacity && table->entries[id].type == type)
      return id;
  }
  return PSX_TYPE_ID_INVALID;
}

static int semantic_type_has_resolved_record_identity(
    const psx_type_t *type) {
  if (!type) return 1;
  if (ps_type_is_tag_aggregate(type) &&
      ps_type_record_id(type) == PSX_RECORD_ID_INVALID)
    return 0;
  if (!semantic_type_has_resolved_record_identity(type->base)) return 0;
  for (int i = 0; i < type->param_count; i++) {
    if (!semantic_type_has_resolved_record_identity(type->param_types[i]))
      return 0;
  }
  return 1;
}

static int semantic_type_node_matches(
    const psx_type_t *canonical, const psx_type_t *candidate) {
  if (!canonical || !candidate || canonical->kind != candidate->kind)
    return 0;
  switch (canonical->kind) {
    case PSX_TYPE_BOOL:
      return canonical->is_unsigned == candidate->is_unsigned;
    case PSX_TYPE_INTEGER:
      if (canonical->integer_kind == PSX_INTEGER_KIND_ENUM ||
          candidate->integer_kind == PSX_INTEGER_KIND_ENUM) {
        return ps_type_tag_identity_matches(canonical, candidate);
      }
      return canonical->is_unsigned == candidate->is_unsigned &&
             canonical->is_plain_char == candidate->is_plain_char &&
             ps_type_integer_rank(canonical) ==
                 ps_type_integer_rank(candidate);
    case PSX_TYPE_FLOAT:
    case PSX_TYPE_COMPLEX:
      return canonical->floating_kind == candidate->floating_kind;
    case PSX_TYPE_POINTER:
      return 1;
    case PSX_TYPE_ARRAY:
      return canonical->array_len == candidate->array_len &&
             canonical->is_vla == candidate->is_vla;
    case PSX_TYPE_STRUCT:
    case PSX_TYPE_UNION:
      return ps_type_record_id(canonical) != PSX_RECORD_ID_INVALID &&
             ps_type_record_id(canonical) ==
                 ps_type_record_id(candidate);
    case PSX_TYPE_FUNCTION:
      return canonical->param_count == candidate->param_count &&
             canonical->is_variadic_function ==
                 candidate->is_variadic_function;
    case PSX_TYPE_VOID:
    case PSX_TYPE_INVALID:
      return 1;
    default:
      return 0;
  }
}

static int semantic_type_entry_matches(
    const psx_semantic_type_table_t *table, psx_type_id_t type_id,
    const psx_type_t *candidate);

static int semantic_type_relation_matches(
    const psx_semantic_type_table_t *table, psx_qual_type_t relation,
    const psx_type_t *candidate) {
  if (!candidate) return relation.type_id == PSX_TYPE_ID_INVALID;
  return relation.type_id != PSX_TYPE_ID_INVALID &&
         relation.qualifiers == ps_type_qualifiers(candidate) &&
         semantic_type_entry_matches(table, relation.type_id, candidate);
}

static int semantic_type_entry_matches(
    const psx_semantic_type_table_t *table, psx_type_id_t type_id,
    const psx_type_t *candidate) {
  if (!table || !candidate || type_id == PSX_TYPE_ID_INVALID ||
      type_id > table->next_id || (size_t)type_id >= table->capacity) {
    return 0;
  }
  const psx_semantic_type_entry_t *entry = &table->entries[type_id];
  if (!semantic_type_node_matches(entry->type, candidate) ||
      !semantic_type_relation_matches(
          table, entry->base_type, candidate->base)) {
    return 0;
  }
  if (candidate->param_count != entry->parameter_count ||
      (candidate->param_count > 0 &&
       (!candidate->param_types || !entry->parameter_types))) {
    return 0;
  }
  for (int i = 0; i < candidate->param_count; i++) {
    if (!semantic_type_relation_matches(
            table, entry->parameter_types[i], candidate->param_types[i])) {
      return 0;
    }
  }
  return 1;
}

psx_qual_type_t psx_semantic_type_table_find(
    const psx_semantic_type_table_t *table, const psx_type_t *type) {
  psx_qual_type_t result = {PSX_TYPE_ID_INVALID,
                            PSX_TYPE_QUALIFIER_NONE};
  if (!table || !type || type->kind == PSX_TYPE_INVALID ||
      !ps_type_is_well_formed(type) ||
      !semantic_type_has_resolved_record_identity(type)) {
    return result;
  }
  result.qualifiers = ps_type_qualifiers(type);
  result.type_id = canonical_type_id(table, type);
  if (result.type_id != PSX_TYPE_ID_INVALID) return result;
  for (psx_type_id_t id = 1; id <= table->next_id; id++) {
    if (semantic_type_entry_matches(table, id, type)) {
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
    table->entries[id].base_type = base;
  }
  if (type->param_count > table->entries[id].parameter_count) {
    psx_qual_type_t *parameter_types = arena_alloc_in(
        table->arena_context,
        (size_t)type->param_count * sizeof(*parameter_types));
    if (!parameter_types) return 0;
    table->entries[id].parameter_types = parameter_types;
    table->entries[id].parameter_count = type->param_count;
  }
  for (int i = 0; i < type->param_count; i++) {
    if (!type->param_types) return 0;
    psx_qual_type_t parameter = psx_semantic_type_table_intern(
        table, type->param_types[i]);
    if (parameter.type_id == PSX_TYPE_ID_INVALID) return 0;
    table->entries[id].parameter_types[i] = parameter;
  }
  const psx_record_decl_t *record = psx_record_decl_table_lookup(
      table->record_decls, ps_type_record_id(type));
  if (record &&
      record->member_count > table->entries[id].record_member_count) {
    psx_qual_type_t *record_member_types = arena_alloc_in(
        table->arena_context,
        (size_t)record->member_count * sizeof(*record_member_types));
    if (!record_member_types) return 0;
    table->entries[id].record_member_types = record_member_types;
    table->entries[id].record_member_count = record->member_count;
  }
  for (int i = 0; record && i < record->member_count; i++) {
    const psx_type_t *member_type =
        psx_record_member_decl_type(&record->members[i]);
    if (!member_type) continue;
    psx_qual_type_t member = psx_semantic_type_table_intern(
        table, member_type);
    if (member.type_id == PSX_TYPE_ID_INVALID) return 0;
    table->entries[id].record_member_types[i] = member;
  }
  return 1;
}

static int materialize_canonical_type_relations(
    psx_semantic_type_table_t *table, psx_type_id_t id) {
  if (!table || id == PSX_TYPE_ID_INVALID || id > table->next_id ||
      (size_t)id >= table->capacity || !table->entries[id].type) {
    return 0;
  }
  psx_semantic_type_entry_t *entry = &table->entries[id];
  if (entry->canonical_relations_materialized) return 1;
  psx_type_t *canonical = entry->type;
  if (entry->base_type.type_id != PSX_TYPE_ID_INVALID) {
    canonical->base = psx_semantic_type_table_lookup(
        table, entry->base_type.type_id);
    if (!canonical->base) return 0;
  } else {
    canonical->base = NULL;
  }
  if (canonical->param_count != entry->parameter_count ||
      (canonical->param_count > 0 && !canonical->param_types)) {
    return 0;
  }
  const psx_type_t **parameters = canonical->param_count > 0
      ? arena_alloc_in(
            table->arena_context,
            (size_t)canonical->param_count * sizeof(*parameters))
      : NULL;
  if (canonical->param_count > 0 && !parameters) return 0;
  for (int i = 0; i < canonical->param_count; i++) {
    parameters[i] = psx_semantic_type_table_lookup(
        table, entry->parameter_types[i].type_id);
    if (!parameters[i]) return 0;
  }
  canonical->param_types = parameters;
  entry->canonical_relations_materialized = 1;
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
  return result && materialize_canonical_type_relations(table, id);
}

psx_qual_type_t psx_semantic_type_table_intern(
    psx_semantic_type_table_t *table, const psx_type_t *type) {
  if (!table || !type || type->kind == PSX_TYPE_INVALID ||
      !ps_type_is_well_formed(type) ||
      !semantic_type_has_resolved_record_identity(type)) {
    return invalid_qual_type();
  }
  psx_type_id_t canonical_id = canonical_type_id(table, type);
  if (canonical_id != PSX_TYPE_ID_INVALID) {
    return (psx_qual_type_t){canonical_id, ps_type_qualifiers(type)};
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
  psx_type_t *canonical = ps_type_clone_in(
      table->arena_context, type);
  if (!canonical) return result;
  ps_type_normalize_scalar_identity(canonical);
  ps_type_remove_all_qualifiers_recursive(canonical);
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
    const psx_semantic_type_table_t *table, psx_qual_type_t relation) {
  if (!table || relation.type_id == PSX_TYPE_ID_INVALID ||
      !psx_semantic_type_table_lookup(table, relation.type_id))
    return invalid_qual_type();
  return relation;
}

psx_qual_type_t psx_semantic_type_table_base(
    const psx_semantic_type_table_t *table, psx_type_id_t type_id) {
  const psx_type_t *type = psx_semantic_type_table_lookup(table, type_id);
  if (!type) return invalid_qual_type();
  return related_type(table, table->entries[type_id].base_type);
}

static psx_qual_type_t semantic_type_table_array_leaf_from(
    const psx_semantic_type_table_t *table, psx_qual_type_t result) {
  result = related_type(table, result);
  const psx_type_t *type = psx_semantic_type_table_lookup(
      table, result.type_id);
  while (type && type->kind == PSX_TYPE_ARRAY) {
    result = psx_semantic_type_table_base(table, result.type_id);
    type = psx_semantic_type_table_lookup(table, result.type_id);
  }
  return type ? result : invalid_qual_type();
}

psx_qual_type_t psx_semantic_type_table_array_leaf(
    const psx_semantic_type_table_t *table, psx_type_id_t type_id) {
  return semantic_type_table_array_leaf_from(
      table, (psx_qual_type_t){type_id, PSX_TYPE_QUALIFIER_NONE});
}

psx_qual_type_t psx_semantic_type_table_pointee_value(
    const psx_semantic_type_table_t *table, psx_type_id_t type_id) {
  const psx_type_t *type = psx_semantic_type_table_lookup(table, type_id);
  if (!type || (type->kind != PSX_TYPE_POINTER &&
                type->kind != PSX_TYPE_ARRAY)) {
    return invalid_qual_type();
  }
  psx_qual_type_t base = psx_semantic_type_table_base(table, type_id);
  return semantic_type_table_array_leaf_from(table, base);
}

psx_qual_type_t psx_semantic_type_table_aggregate_object(
    const psx_semantic_type_table_t *table, psx_qual_type_t type) {
  while (type.type_id != PSX_TYPE_ID_INVALID) {
    const psx_type_t *current = psx_semantic_type_table_lookup(
        table, type.type_id);
    if (!current) return invalid_qual_type();
    if (ps_type_is_tag_aggregate(current)) return type;
    if (current->kind != PSX_TYPE_POINTER &&
        current->kind != PSX_TYPE_ARRAY)
      return invalid_qual_type();
    type = psx_semantic_type_table_base(table, type.type_id);
  }
  return invalid_qual_type();
}

psx_qual_type_t psx_semantic_type_table_parameter(
    const psx_semantic_type_table_t *table, psx_type_id_t type_id,
    int parameter_index) {
  const psx_type_t *type = psx_semantic_type_table_lookup(table, type_id);
  if (!type || parameter_index < 0 ||
      parameter_index >= table->entries[type_id].parameter_count ||
      !table->entries[type_id].parameter_types)
    return invalid_qual_type();
  return related_type(
      table, table->entries[type_id].parameter_types[parameter_index]);
}

psx_qual_type_t psx_semantic_type_table_record_member(
    const psx_semantic_type_table_t *table, psx_type_id_t type_id,
    int member_index) {
  const psx_type_t *type = psx_semantic_type_table_lookup(table, type_id);
  if (!type || !ps_type_is_tag_aggregate(type) || member_index < 0 ||
      member_index >= table->entries[type_id].record_member_count)
    return invalid_qual_type();
  return related_type(
      table, table->entries[type_id].record_member_types[member_index]);
}
