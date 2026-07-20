#include "type_compatibility_view.h"

#include "../parser/arena.h"
#include "../parser/type.h"
#include "type_identity_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { PSX_QUALIFIER_VIEW_COUNT = 8 };

typedef struct {
  const psx_type_t *canonical_view;
  const psx_type_t *views[PSX_QUALIFIER_VIEW_COUNT];
  int record_tag_scope_depth_p1;
  unsigned char canonical_materializing;
  unsigned char materializing[PSX_QUALIFIER_VIEW_COUNT];
} psx_type_compatibility_entry_t;

struct psx_type_compatibility_cache_t {
  arena_context_t *arena_context;
  psx_type_compatibility_entry_t *entries;
  size_t capacity;
};

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

static int reserve_type_id(
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

static psx_qual_type_t invalid_qual_type(void) {
  return (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                           PSX_TYPE_QUALIFIER_NONE};
}

static psx_type_shape_t parser_type_shape(const psx_type_t *type) {
  psx_type_shape_t shape = {0};
  if (!type) return shape;
  shape.kind = type->kind;
  shape.array_len = type->array_len;
  shape.integer_kind = type->integer_kind;
  if (shape.kind == PSX_TYPE_INTEGER &&
      shape.integer_kind == PSX_INTEGER_KIND_NONE)
    shape.integer_kind = PSX_INTEGER_KIND_INT;
  shape.floating_kind = type->floating_kind;
  shape.record_id = ps_type_record_id(type);
  if (type->kind == PSX_TYPE_STRUCT || type->kind == PSX_TYPE_UNION) {
    shape.record_tag_name = type->tag_name;
    shape.record_tag_length = type->tag_len;
  } else if (type->kind == PSX_TYPE_INTEGER &&
             type->integer_kind == PSX_INTEGER_KIND_ENUM) {
    shape.enum_tag_name = type->tag_name;
    shape.enum_tag_length = type->tag_len;
    shape.enum_tag_scope_depth_p1 = type->tag_scope_depth_p1;
  }
  shape.parameter_count = type->param_count;
  shape.is_unsigned = type->is_unsigned;
  shape.is_plain_char = type->is_plain_char;
  shape.is_vla = type->is_vla;
  shape.has_function_prototype = type->has_function_prototype;
  shape.is_variadic_function = type->is_variadic_function;
  return shape;
}

static int parser_type_has_resolved_record_identity(
    const psx_type_t *type) {
  if (!type) return 1;
  if (ps_type_is_tag_aggregate(type) &&
      ps_type_record_id(type) == PSX_RECORD_ID_INVALID)
    return 0;
  if (!parser_type_has_resolved_record_identity(type->base)) return 0;
  for (int i = 0; i < type->param_count; i++) {
    if (!parser_type_has_resolved_record_identity(type->param_types[i]))
      return 0;
  }
  return 1;
}

static int parser_type_can_import(const psx_type_t *type) {
  return type && type->kind != PSX_TYPE_INVALID &&
         ps_type_is_well_formed(type) &&
         parser_type_has_resolved_record_identity(type);
}

psx_qual_type_t psx_semantic_type_table_find(
    const psx_semantic_type_table_t *table, const psx_type_t *type) {
  if (!table || !parser_type_can_import(type))
    return invalid_qual_type();
  psx_qual_type_t compatibility_identity =
      psx_type_compatibility_view_identity(
          psx_semantic_type_table_compatibility_cache_const(table), type);
  if (compatibility_identity.type_id != PSX_TYPE_ID_INVALID)
    return compatibility_identity;

  psx_qual_type_t base_type = invalid_qual_type();
  if (type->base) {
    base_type = psx_semantic_type_table_find(table, type->base);
    if (base_type.type_id == PSX_TYPE_ID_INVALID)
      return invalid_qual_type();
  }
  psx_qual_type_t *parameter_types = NULL;
  if (type->param_count > 0) {
    parameter_types = malloc(
        (size_t)type->param_count * sizeof(*parameter_types));
    if (!parameter_types) return invalid_qual_type();
    for (int i = 0; i < type->param_count; i++) {
      parameter_types[i] = psx_semantic_type_table_find(
          table, type->param_types[i]);
      if (parameter_types[i].type_id == PSX_TYPE_ID_INVALID) {
        free(parameter_types);
        return invalid_qual_type();
      }
    }
  }
  psx_type_shape_t shape = parser_type_shape(type);
  psx_qual_type_t result = psx_semantic_type_table_find_shape(
      table, &shape, base_type, parameter_types, type->param_count);
  free(parameter_types);
  if (result.type_id != PSX_TYPE_ID_INVALID)
    result.qualifiers = ps_type_qualifiers(type);
  return result;
}

psx_qual_type_t psx_semantic_type_table_intern(
    psx_semantic_type_table_t *table, const psx_type_t *type) {
  if (!table || !parser_type_can_import(type))
    return invalid_qual_type();
  psx_type_compatibility_cache_t *cache =
      psx_semantic_type_table_compatibility_cache(table);
  psx_qual_type_t result = psx_type_compatibility_view_identity(
      cache, type);
  if (result.type_id != PSX_TYPE_ID_INVALID) {
    return psx_type_compatibility_cache_remember_import(
               cache, result, type)
               ? result
               : invalid_qual_type();
  }

  psx_qual_type_t base_type = invalid_qual_type();
  if (type->base) {
    base_type = psx_semantic_type_table_intern(table, type->base);
    if (base_type.type_id == PSX_TYPE_ID_INVALID)
      return invalid_qual_type();
  } else if (type->kind == PSX_TYPE_COMPLEX) {
    base_type = psx_semantic_type_table_fundamental_floating(
        table, type->floating_kind, 0);
    if (base_type.type_id == PSX_TYPE_ID_INVALID)
      return invalid_qual_type();
  }
  psx_qual_type_t *parameter_types = NULL;
  if (type->param_count > 0) {
    parameter_types = malloc(
        (size_t)type->param_count * sizeof(*parameter_types));
    if (!parameter_types) return invalid_qual_type();
    for (int i = 0; i < type->param_count; i++) {
      parameter_types[i] = psx_semantic_type_table_intern(
          table, type->param_types[i]);
      if (parameter_types[i].type_id == PSX_TYPE_ID_INVALID) {
        free(parameter_types);
        return invalid_qual_type();
      }
    }
  }
  psx_type_shape_t shape = parser_type_shape(type);
  result = psx_semantic_type_table_intern_shape(
      table, &shape, base_type, parameter_types, type->param_count);
  free(parameter_types);
  if (result.type_id != PSX_TYPE_ID_INVALID) {
    result.qualifiers = ps_type_qualifiers(type);
    if (!psx_type_compatibility_cache_remember_import(
            cache, result, type))
      return invalid_qual_type();
  }
  return result;
}

int psx_type_compatibility_cache_remember_import(
    psx_type_compatibility_cache_t *cache, psx_qual_type_t identity,
    const psx_type_t *source) {
  if (!cache || !source || identity.type_id == PSX_TYPE_ID_INVALID ||
      !reserve_type_id(cache, identity.type_id))
    return 0;
  if (source->kind != PSX_TYPE_STRUCT && source->kind != PSX_TYPE_UNION)
    return 1;
  psx_type_compatibility_entry_t *entry = &cache->entries[identity.type_id];
  if (source->tag_scope_depth_p1 > 0 &&
      entry->record_tag_scope_depth_p1 == 0)
    entry->record_tag_scope_depth_p1 = source->tag_scope_depth_p1;
  return entry->record_tag_scope_depth_p1 == 0 ||
         source->tag_scope_depth_p1 == 0 ||
         entry->record_tag_scope_depth_p1 == source->tag_scope_depth_p1;
}

static const psx_type_t *materialize_view(
    psx_type_compatibility_cache_t *cache,
    const psx_semantic_type_table_t *types, psx_qual_type_t type,
    int preserve_relation_qualifiers) {
  const psx_type_qualifiers_t supported =
      PSX_TYPE_QUALIFIER_CONST | PSX_TYPE_QUALIFIER_VOLATILE |
      PSX_TYPE_QUALIFIER_ATOMIC;
  psx_type_shape_t shape = {0};
  if (!cache || !types || type.type_id == PSX_TYPE_ID_INVALID ||
      (type.qualifiers & ~supported) != 0 ||
      !psx_semantic_type_table_describe(types, type.type_id, &shape) ||
      !reserve_type_id(cache, type.type_id))
    return NULL;

  unsigned view_index = type.qualifiers;
  psx_type_compatibility_entry_t *entry = &cache->entries[type.type_id];
  if (preserve_relation_qualifiers) {
    if (entry->views[view_index]) return entry->views[view_index];
    if (entry->materializing[view_index]) return NULL;
    entry->materializing[view_index] = 1;
  } else {
    if (entry->canonical_view) return entry->canonical_view;
    if (entry->canonical_materializing) return NULL;
    entry->canonical_materializing = 1;
  }

  psx_type_t *view = arena_alloc_in(
      cache->arena_context, sizeof(*view));
  if (!view) goto fail;
  *view = (psx_type_t){
      .kind = shape.kind,
      .array_len = shape.array_len,
      .integer_kind = shape.integer_kind,
      .floating_kind = shape.floating_kind,
      .record_id = shape.record_id,
      .qualifiers = preserve_relation_qualifiers
                        ? type.qualifiers
                        : PSX_TYPE_QUALIFIER_NONE,
      .is_unsigned = shape.is_unsigned,
      .is_plain_char = shape.is_plain_char,
      .is_vla = shape.is_vla,
      .param_count = shape.parameter_count,
      .has_function_prototype = shape.has_function_prototype,
      .is_variadic_function = shape.is_variadic_function,
  };
  if (shape.kind == PSX_TYPE_STRUCT || shape.kind == PSX_TYPE_UNION) {
    view->tag_name = (char *)shape.record_tag_name;
    view->tag_len = shape.record_tag_length;
    view->tag_scope_depth_p1 = entry->record_tag_scope_depth_p1;
  } else if (shape.kind == PSX_TYPE_INTEGER &&
             shape.integer_kind == PSX_INTEGER_KIND_ENUM) {
    view->tag_name = (char *)shape.enum_tag_name;
    view->tag_len = shape.enum_tag_length;
    view->tag_scope_depth_p1 = shape.enum_tag_scope_depth_p1;
  }

  psx_qual_type_t base = psx_semantic_type_table_base(
      types, type.type_id);
  if (base.type_id != PSX_TYPE_ID_INVALID) {
    if (!preserve_relation_qualifiers)
      base.qualifiers = PSX_TYPE_QUALIFIER_NONE;
    view->base = materialize_view(
        cache, types, base, preserve_relation_qualifiers);
    if (!view->base) goto fail;
  }
  if (shape.parameter_count > 0) {
    const psx_type_t **parameters = arena_alloc_in(
        cache->arena_context,
        (size_t)shape.parameter_count * sizeof(*parameters));
    if (!parameters) goto fail;
    for (int i = 0; i < shape.parameter_count; i++) {
      psx_qual_type_t parameter = psx_semantic_type_table_parameter(
          types, type.type_id, i);
      if (!preserve_relation_qualifiers)
        parameter.qualifiers = PSX_TYPE_QUALIFIER_NONE;
      parameters[i] = materialize_view(
          cache, types, parameter, preserve_relation_qualifiers);
      if (!parameters[i]) goto fail;
    }
    view->param_types = parameters;
  }
  if (preserve_relation_qualifiers &&
      type.qualifiers == PSX_TYPE_QUALIFIER_NONE) {
    const psx_type_t *canonical = materialize_view(
        cache, types, type, 0);
    int matches_canonical = canonical && view->base == canonical->base &&
                            view->param_count == canonical->param_count;
    for (int i = 0; matches_canonical && i < view->param_count; i++) {
      if (view->param_types[i] != canonical->param_types[i])
        matches_canonical = 0;
    }
    if (matches_canonical) {
      entry = &cache->entries[type.type_id];
      entry->views[view_index] = canonical;
      entry->materializing[view_index] = 0;
      return canonical;
    }
  }
  entry = &cache->entries[type.type_id];
  if (preserve_relation_qualifiers) {
    entry->views[view_index] = view;
    entry->materializing[view_index] = 0;
  } else {
    entry->canonical_view = view;
    entry->canonical_materializing = 0;
  }
  return view;

fail:
  entry = &cache->entries[type.type_id];
  if (preserve_relation_qualifiers) {
    entry->views[view_index] = NULL;
    entry->materializing[view_index] = 0;
  } else {
    entry->canonical_view = NULL;
    entry->canonical_materializing = 0;
  }
  return NULL;
}

const psx_type_t *psx_type_compatibility_view(
    psx_type_compatibility_cache_t *cache,
    const psx_semantic_type_table_t *types, psx_qual_type_t type) {
  return materialize_view(cache, types, type, 1);
}

const psx_type_t *psx_type_compatibility_canonical_view(
    psx_type_compatibility_cache_t *cache,
    const psx_semantic_type_table_t *types, psx_type_id_t type_id) {
  return materialize_view(
      cache, types,
      (psx_qual_type_t){type_id, PSX_TYPE_QUALIFIER_NONE}, 0);
}

psx_qual_type_t psx_type_compatibility_view_identity(
    const psx_type_compatibility_cache_t *cache,
    const psx_type_t *view) {
  if (!cache || !view)
    return (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                             PSX_TYPE_QUALIFIER_NONE};
  for (psx_type_id_t id = 1; (size_t)id < cache->capacity; id++) {
    const psx_type_compatibility_entry_t *entry = &cache->entries[id];
    if (entry->canonical_view == view)
      return (psx_qual_type_t){id, PSX_TYPE_QUALIFIER_NONE};
    for (unsigned qualifiers = 0;
         qualifiers < PSX_QUALIFIER_VIEW_COUNT; qualifiers++) {
      if (entry->views[qualifiers] == view)
        return (psx_qual_type_t){id, qualifiers};
    }
  }
  return (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                           PSX_TYPE_QUALIFIER_NONE};
}
