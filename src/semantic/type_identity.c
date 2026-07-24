#include "type_identity_internal.h"

#include "../parser/arena.h"
#include "../type_system/integer_conversion.h"
#include "record_decl_table.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  psx_type_shape_t shape;
  psx_qual_type_t base_type;
  psx_qual_type_t *parameter_types;
  int parameter_count;
} psx_semantic_type_entry_t;

struct psx_semantic_type_table_t {
  arena_context_t *arena_context;
  const psx_record_decl_table_t *record_decls;
  psx_semantic_type_entry_t *entries;
  size_t capacity;
  psx_type_id_t next_id;
};

static int seed_fundamental_types(psx_semantic_type_table_t *table);

static int semantic_type_id_is_valid(
    const psx_semantic_type_table_t *table, psx_type_id_t type_id) {
  return table && type_id != PSX_TYPE_ID_INVALID &&
         type_id <= table->next_id && (size_t)type_id < table->capacity &&
         table->entries[type_id].shape.kind != PSX_TYPE_INVALID;
}

psx_semantic_type_table_t *psx_semantic_type_table_create(void) {
  psx_semantic_type_table_t *table = calloc(1, sizeof(*table));
  if (!table) return NULL;
  table->arena_context = arena_context_create();
  if (!table->arena_context) {
    free(table);
    return NULL;
  }
  if (!seed_fundamental_types(table)) {
    psx_semantic_type_table_destroy(table);
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
  (void)seed_fundamental_types(table);
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

static int semantic_type_shapes_match(
    const psx_type_shape_t *canonical,
    const psx_type_shape_t *candidate) {
  if (!canonical || !candidate || canonical->kind != candidate->kind)
    return 0;
  switch (canonical->kind) {
    case PSX_TYPE_BOOL:
      return canonical->is_unsigned == candidate->is_unsigned;
    case PSX_TYPE_INTEGER:
      if (canonical->integer_kind == PSX_INTEGER_KIND_ENUM ||
          candidate->integer_kind == PSX_INTEGER_KIND_ENUM) {
        return canonical->integer_kind == PSX_INTEGER_KIND_ENUM &&
               candidate->integer_kind == PSX_INTEGER_KIND_ENUM &&
               canonical->enum_decl_id != PSX_DECL_ID_INVALID &&
               canonical->enum_decl_id == candidate->enum_decl_id;
      }
      return canonical->is_unsigned == candidate->is_unsigned &&
             canonical->is_plain_char == candidate->is_plain_char &&
             psx_integer_conversion_from_shape(canonical).rank ==
                 psx_integer_conversion_from_shape(candidate).rank;
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
      return canonical->record_id != PSX_RECORD_ID_INVALID &&
             canonical->record_id == candidate->record_id;
    case PSX_TYPE_FUNCTION:
      return canonical->parameter_count == candidate->parameter_count &&
             canonical->has_function_prototype ==
                 candidate->has_function_prototype &&
             canonical->is_variadic_function ==
                 candidate->is_variadic_function;
    case PSX_TYPE_VOID:
    case PSX_TYPE_INVALID:
      return 1;
    default:
      return 0;
  }
}

static int semantic_type_relations_match(
    const psx_semantic_type_entry_t *entry,
    psx_qual_type_t base_type,
    const psx_qual_type_t *parameter_types,
    int parameter_count) {
  if (!entry || entry->parameter_count != parameter_count ||
      entry->base_type.type_id != base_type.type_id ||
      entry->base_type.qualifiers != base_type.qualifiers)
    return 0;
  for (int i = 0; i < parameter_count; i++) {
    if (entry->parameter_types[i].type_id != parameter_types[i].type_id ||
        entry->parameter_types[i].qualifiers !=
            parameter_types[i].qualifiers)
      return 0;
  }
  return 1;
}

static psx_type_id_t semantic_type_shape_id(
    const psx_semantic_type_table_t *table,
    const psx_type_shape_t *shape, psx_qual_type_t base_type,
    const psx_qual_type_t *parameter_types, int parameter_count) {
  if (!table || !shape) return PSX_TYPE_ID_INVALID;
  for (psx_type_id_t id = 1; id <= table->next_id; id++) {
    if (!semantic_type_id_is_valid(table, id)) continue;
    const psx_semantic_type_entry_t *entry = &table->entries[id];
    if (semantic_type_shapes_match(&entry->shape, shape) &&
        semantic_type_relations_match(
            entry, base_type, parameter_types, parameter_count))
      return id;
  }
  return PSX_TYPE_ID_INVALID;
}

static int semantic_type_shape_own_name(
    psx_semantic_type_table_t *table, const char *source, int length,
    const char **owned) {
  if (!table || !owned || length < 0 || (length > 0 && !source)) return 0;
  *owned = NULL;
  if (length == 0) return 1;
  char *copy = arena_alloc_in(
      table->arena_context, (size_t)length + 1);
  if (!copy) return 0;
  memcpy(copy, source, (size_t)length);
  copy[length] = '\0';
  *owned = copy;
  return 1;
}

static int semantic_type_shape_own_names(
    psx_semantic_type_table_t *table, psx_type_shape_t *shape) {
  if (!table || !shape) return 0;
  if (!semantic_type_shape_own_name(
          table, shape->record_tag_name, shape->record_tag_length,
          &shape->record_tag_name))
    return 0;
  return semantic_type_shape_own_name(
      table, shape->enum_tag_name, shape->enum_tag_length,
      &shape->enum_tag_name);
}

static int semantic_type_shape_request_is_valid(
    const psx_semantic_type_table_t *table,
    const psx_type_shape_t *shape, psx_qual_type_t base_type,
    const psx_qual_type_t *parameter_types, int parameter_count) {
  int requires_base = shape &&
      (shape->kind == PSX_TYPE_COMPLEX ||
       shape->kind == PSX_TYPE_POINTER ||
       shape->kind == PSX_TYPE_ARRAY ||
       shape->kind == PSX_TYPE_FUNCTION);
  int has_base = table &&
      semantic_type_id_is_valid(table, base_type.type_id);
  if (!table || !shape || parameter_count < 0 ||
      shape->parameter_count != parameter_count ||
      requires_base != has_base ||
      (base_type.type_id == PSX_TYPE_ID_INVALID &&
       base_type.qualifiers != PSX_TYPE_QUALIFIER_NONE) ||
      (shape->kind != PSX_TYPE_FUNCTION && parameter_count != 0) ||
      (parameter_count > 0 && !parameter_types))
    return 0;
  for (int i = 0; i < parameter_count; i++) {
    if (!semantic_type_id_is_valid(table, parameter_types[i].type_id))
      return 0;
  }
  return 1;
}

psx_qual_type_t psx_semantic_type_table_find_shape(
    const psx_semantic_type_table_t *table,
    const psx_type_shape_t *shape, psx_qual_type_t base_type,
    const psx_qual_type_t *parameter_types, int parameter_count) {
  if (!semantic_type_shape_request_is_valid(
          table, shape, base_type, parameter_types, parameter_count))
    return invalid_qual_type();
  psx_type_id_t existing = semantic_type_shape_id(
      table, shape, base_type, parameter_types, parameter_count);
  return (psx_qual_type_t){existing, PSX_TYPE_QUALIFIER_NONE};
}

psx_qual_type_t psx_semantic_type_table_intern_shape(
    psx_semantic_type_table_t *table,
    const psx_type_shape_t *shape, psx_qual_type_t base_type,
    const psx_qual_type_t *parameter_types, int parameter_count) {
  psx_qual_type_t existing = psx_semantic_type_table_find_shape(
      table, shape, base_type, parameter_types, parameter_count);
  if (existing.type_id != PSX_TYPE_ID_INVALID) return existing;
  if (!semantic_type_shape_request_is_valid(
          table, shape, base_type, parameter_types, parameter_count))
    return invalid_qual_type();
  if (table->next_id == UINT_MAX) return invalid_qual_type();
  psx_type_id_t id = table->next_id + 1;
  if (!reserve_type_id(table, id)) return invalid_qual_type();
  psx_type_shape_t owned_shape = *shape;
  if (!semantic_type_shape_own_names(table, &owned_shape))
    return invalid_qual_type();
  psx_qual_type_t *owned_parameters = NULL;
  if (parameter_count > 0) {
    owned_parameters = arena_alloc_in(
        table->arena_context,
        (size_t)parameter_count * sizeof(*owned_parameters));
    if (!owned_parameters) return invalid_qual_type();
    memcpy(owned_parameters, parameter_types,
           (size_t)parameter_count * sizeof(*owned_parameters));
  }
  table->entries[id].shape = owned_shape;
  table->entries[id].base_type = base_type;
  table->entries[id].parameter_types = owned_parameters;
  table->entries[id].parameter_count = parameter_count;
  table->next_id = id;
  return (psx_qual_type_t){id, PSX_TYPE_QUALIFIER_NONE};
}

psx_qual_type_t psx_semantic_type_table_intern_integer(
    psx_semantic_type_table_t *table,
    psx_integer_kind_t integer_kind, int is_unsigned,
    int is_plain_char) {
  if (integer_kind <= PSX_INTEGER_KIND_NONE ||
      integer_kind >= PSX_INTEGER_KIND_ENUM)
    return invalid_qual_type();
  psx_type_shape_t shape = {
      .kind = integer_kind == PSX_INTEGER_KIND_BOOL
                  ? PSX_TYPE_BOOL : PSX_TYPE_INTEGER,
      .integer_kind = integer_kind,
      .is_unsigned = is_unsigned ? 1 : 0,
      .is_plain_char = integer_kind == PSX_INTEGER_KIND_CHAR &&
                               is_plain_char
                           ? 1 : 0,
  };
  return psx_semantic_type_table_intern_shape(
      table, &shape, invalid_qual_type(), NULL, 0);
}

psx_qual_type_t psx_semantic_type_table_intern_floating(
    psx_semantic_type_table_t *table,
    psx_floating_kind_t floating_kind, int is_complex) {
  if (floating_kind <= PSX_FLOATING_KIND_NONE ||
      floating_kind > PSX_FLOATING_KIND_LONG_DOUBLE)
    return invalid_qual_type();
  psx_qual_type_t base_type = invalid_qual_type();
  if (is_complex) {
    base_type = psx_semantic_type_table_intern_floating(
        table, floating_kind, 0);
    if (base_type.type_id == PSX_TYPE_ID_INVALID)
      return invalid_qual_type();
  }
  psx_type_shape_t shape = {
      .kind = is_complex ? PSX_TYPE_COMPLEX : PSX_TYPE_FLOAT,
      .floating_kind = floating_kind,
  };
  return psx_semantic_type_table_intern_shape(
      table, &shape, base_type, NULL, 0);
}

psx_qual_type_t psx_semantic_type_table_fundamental_integer(
    const psx_semantic_type_table_t *table,
    psx_integer_kind_t integer_kind, int is_unsigned,
    int is_plain_char) {
  if (integer_kind <= PSX_INTEGER_KIND_NONE ||
      integer_kind >= PSX_INTEGER_KIND_ENUM)
    return invalid_qual_type();
  const psx_type_shape_t shape = {
      .kind = integer_kind == PSX_INTEGER_KIND_BOOL
                  ? PSX_TYPE_BOOL : PSX_TYPE_INTEGER,
      .integer_kind = integer_kind,
      .is_unsigned = is_unsigned ? 1 : 0,
      .is_plain_char = integer_kind == PSX_INTEGER_KIND_CHAR &&
                               is_plain_char
                           ? 1 : 0,
  };
  return (psx_qual_type_t){
      semantic_type_shape_id(
          table, &shape, invalid_qual_type(), NULL, 0),
      PSX_TYPE_QUALIFIER_NONE,
  };
}

psx_qual_type_t psx_semantic_type_table_fundamental_floating(
    const psx_semantic_type_table_t *table,
    psx_floating_kind_t floating_kind, int is_complex) {
  if (floating_kind <= PSX_FLOATING_KIND_NONE ||
      floating_kind > PSX_FLOATING_KIND_LONG_DOUBLE)
    return invalid_qual_type();
  psx_qual_type_t base_type = is_complex
      ? psx_semantic_type_table_fundamental_floating(
            table, floating_kind, 0)
      : invalid_qual_type();
  if (is_complex && base_type.type_id == PSX_TYPE_ID_INVALID)
    return invalid_qual_type();
  const psx_type_shape_t shape = {
      .kind = is_complex ? PSX_TYPE_COMPLEX : PSX_TYPE_FLOAT,
      .floating_kind = floating_kind,
  };
  return (psx_qual_type_t){
      semantic_type_shape_id(table, &shape, base_type, NULL, 0),
      PSX_TYPE_QUALIFIER_NONE,
  };
}

static int seed_fundamental_types(psx_semantic_type_table_t *table) {
  if (!table) return 0;
  if (psx_semantic_type_table_intern_void(table).type_id ==
      PSX_TYPE_ID_INVALID)
    return 0;
  for (psx_integer_kind_t kind = PSX_INTEGER_KIND_BOOL;
       kind < PSX_INTEGER_KIND_ENUM; kind++) {
    int last_unsigned = kind == PSX_INTEGER_KIND_BOOL ? 0 : 1;
    for (int is_unsigned = 0; is_unsigned <= last_unsigned;
         is_unsigned++) {
      if (psx_semantic_type_table_intern_integer(
              table, kind, is_unsigned, 0).type_id ==
          PSX_TYPE_ID_INVALID)
        return 0;
    }
  }
  if (psx_semantic_type_table_intern_integer(
          table, PSX_INTEGER_KIND_CHAR, 0, 1).type_id ==
      PSX_TYPE_ID_INVALID)
    return 0;
  for (psx_floating_kind_t kind = PSX_FLOATING_KIND_FLOAT;
       kind <= PSX_FLOATING_KIND_LONG_DOUBLE; kind++) {
    if (psx_semantic_type_table_intern_floating(
            table, kind, 0).type_id == PSX_TYPE_ID_INVALID ||
        psx_semantic_type_table_intern_floating(
            table, kind, 1).type_id == PSX_TYPE_ID_INVALID)
      return 0;
  }
  return 1;
}

psx_qual_type_t psx_semantic_type_table_intern_void(
    psx_semantic_type_table_t *table) {
  const psx_type_shape_t shape = {.kind = PSX_TYPE_VOID};
  return psx_semantic_type_table_intern_shape(
      table, &shape, invalid_qual_type(), NULL, 0);
}

psx_qual_type_t psx_semantic_type_table_intern_enum(
    psx_semantic_type_table_t *table, psx_decl_id_t declaration_id,
    const char *tag_name, int tag_length, int is_unsigned) {
  if (tag_length < 0 || (tag_length > 0 && !tag_name) ||
      declaration_id == PSX_DECL_ID_INVALID)
    return invalid_qual_type();
  const psx_type_shape_t shape = {
      .kind = PSX_TYPE_INTEGER,
      .integer_kind = PSX_INTEGER_KIND_ENUM,
      .enum_tag_name = tag_name,
      .enum_tag_length = tag_length,
      .enum_decl_id = declaration_id,
      .is_unsigned = is_unsigned ? 1 : 0,
  };
  return psx_semantic_type_table_intern_shape(
      table, &shape, invalid_qual_type(), NULL, 0);
}

psx_qual_type_t psx_semantic_type_table_intern_record(
    psx_semantic_type_table_t *table, psx_record_id_t record_id) {
  const psx_record_decl_t *record = table
      ? psx_record_decl_table_lookup(table->record_decls, record_id)
      : NULL;
  if (!record ||
      (record->record_kind != PSX_TYPE_STRUCT &&
       record->record_kind != PSX_TYPE_UNION))
    return invalid_qual_type();
  const psx_type_shape_t shape = {
      .kind = record->record_kind,
      .record_id = record->record_id,
      .record_tag_name = record->tag_name,
      .record_tag_length = record->tag_len,
  };
  return psx_semantic_type_table_intern_shape(
      table, &shape, invalid_qual_type(), NULL, 0);
}

psx_qual_type_t psx_semantic_type_table_intern_pointer_to(
    psx_semantic_type_table_t *table, psx_qual_type_t pointee) {
  const psx_type_shape_t shape = {.kind = PSX_TYPE_POINTER};
  return psx_semantic_type_table_intern_shape(
      table, &shape, pointee, NULL, 0);
}

psx_qual_type_t psx_semantic_type_table_intern_array_of(
    psx_semantic_type_table_t *table, psx_qual_type_t element,
    int array_len, int is_vla) {
  if (array_len < 0) return invalid_qual_type();
  const psx_type_shape_t shape = {
      .kind = PSX_TYPE_ARRAY,
      .array_len = array_len,
      .is_vla = is_vla ? 1 : 0,
  };
  return psx_semantic_type_table_intern_shape(
      table, &shape, element, NULL, 0);
}

psx_qual_type_t psx_semantic_type_table_intern_function(
    psx_semantic_type_table_t *table, psx_qual_type_t result,
    const psx_qual_type_t *parameters, int parameter_count,
    int has_prototype, int is_variadic) {
  if (parameter_count < 0 ||
      (parameter_count > 0 && !parameters))
    return invalid_qual_type();
  psx_type_shape_t result_shape = {0};
  if (!psx_semantic_type_table_describe(
          table, result.type_id, &result_shape) ||
      result_shape.kind == PSX_TYPE_ARRAY ||
      result_shape.kind == PSX_TYPE_FUNCTION)
    return invalid_qual_type();
  const psx_type_shape_t shape = {
      .kind = PSX_TYPE_FUNCTION,
      .parameter_count = parameter_count,
      .has_function_prototype = has_prototype ? 1 : 0,
      .is_variadic_function = is_variadic ? 1 : 0,
  };
  return psx_semantic_type_table_intern_shape(
      table, &shape, result, parameters, parameter_count);
}

int psx_semantic_type_table_describe(
    const psx_semantic_type_table_t *table, psx_type_id_t type_id,
    psx_type_shape_t *out) {
  if (!out || !semantic_type_id_is_valid(table, type_id)) {
    return 0;
  }
  *out = table->entries[type_id].shape;
  return 1;
}

int psx_semantic_type_table_qual_type_is_valid(
    const psx_semantic_type_table_t *table, psx_qual_type_t type) {
  const psx_type_qualifiers_t supported =
      PSX_TYPE_QUALIFIER_CONST | PSX_TYPE_QUALIFIER_VOLATILE |
      PSX_TYPE_QUALIFIER_ATOMIC | PSX_TYPE_QUALIFIER_RESTRICT;
  return (type.qualifiers & ~supported) == 0 &&
         semantic_type_id_is_valid(table, type.type_id);
}

int psx_semantic_type_table_pointer_can_be_restrict_qualified(
    const psx_semantic_type_table_t *table, psx_type_id_t type_id) {
  psx_type_shape_t pointer_shape = {0};
  if (!psx_semantic_type_table_describe(
          table, type_id, &pointer_shape) ||
      pointer_shape.kind != PSX_TYPE_POINTER)
    return 0;
  psx_qual_type_t pointee =
      psx_semantic_type_table_base(table, type_id);
  psx_type_shape_t pointee_shape = {0};
  return pointee.type_id != PSX_TYPE_ID_INVALID &&
         psx_semantic_type_table_describe(
             table, pointee.type_id, &pointee_shape) &&
         pointee_shape.kind != PSX_TYPE_FUNCTION;
}

int psx_semantic_type_table_has_invalid_restrict_qualification(
    const psx_semantic_type_table_t *table, psx_qual_type_t type) {
  psx_type_shape_t shape = {0};
  if (!psx_semantic_type_table_describe(
          table, type.type_id, &shape))
    return 1;
  if ((type.qualifiers & PSX_TYPE_QUALIFIER_RESTRICT) != 0 &&
      !psx_semantic_type_table_pointer_can_be_restrict_qualified(
          table, type.type_id))
    return 1;
  if (shape.kind == PSX_TYPE_POINTER ||
      shape.kind == PSX_TYPE_ARRAY ||
      shape.kind == PSX_TYPE_FUNCTION) {
    psx_qual_type_t base =
        psx_semantic_type_table_base(table, type.type_id);
    if (base.type_id == PSX_TYPE_ID_INVALID ||
        psx_semantic_type_table_has_invalid_restrict_qualification(
            table, base))
      return 1;
  }
  if (shape.kind == PSX_TYPE_FUNCTION) {
    for (int i = 0; i < shape.parameter_count; i++) {
      psx_qual_type_t parameter =
          psx_semantic_type_table_parameter(table, type.type_id, i);
      if (parameter.type_id == PSX_TYPE_ID_INVALID ||
          psx_semantic_type_table_has_invalid_restrict_qualification(
              table, parameter))
        return 1;
    }
  }
  return 0;
}

static int semantic_qual_types_match(
    const psx_semantic_type_table_t *table,
    psx_qual_type_t left, psx_qual_type_t right,
    int compare_qualifiers) {
  if (!psx_semantic_type_table_qual_type_is_valid(table, left) ||
      !psx_semantic_type_table_qual_type_is_valid(table, right))
    return 0;
  if (compare_qualifiers && left.qualifiers != right.qualifiers) return 0;
  if (left.type_id == right.type_id) return 1;

  const psx_semantic_type_entry_t *left_entry =
      &table->entries[left.type_id];
  const psx_semantic_type_entry_t *right_entry =
      &table->entries[right.type_id];
  const psx_type_shape_t *left_shape = &left_entry->shape;
  const psx_type_shape_t *right_shape = &right_entry->shape;
  if (left_shape->kind != right_shape->kind) return 0;

  switch (left_shape->kind) {
    case PSX_TYPE_POINTER:
      return semantic_qual_types_match(
          table, left_entry->base_type, right_entry->base_type, 1);
    case PSX_TYPE_ARRAY:
      return left_shape->array_len == right_shape->array_len &&
             left_shape->is_vla == right_shape->is_vla &&
             semantic_qual_types_match(
                 table, left_entry->base_type, right_entry->base_type, 1);
    case PSX_TYPE_FUNCTION:
      if (left_entry->parameter_count != right_entry->parameter_count ||
          left_shape->is_variadic_function !=
              right_shape->is_variadic_function ||
          !semantic_qual_types_match(
              table, left_entry->base_type, right_entry->base_type, 1))
        return 0;
      for (int i = 0; i < left_entry->parameter_count; i++) {
        if (!semantic_qual_types_match(
                table, left_entry->parameter_types[i],
                right_entry->parameter_types[i], 1))
          return 0;
      }
      return 1;
    default:
      return semantic_type_shapes_match(left_shape, right_shape);
  }
}

int psx_semantic_type_table_unqualified_types_match(
    const psx_semantic_type_table_t *table,
    psx_qual_type_t left, psx_qual_type_t right) {
  return semantic_qual_types_match(table, left, right, 0);
}

static int semantic_qual_types_compatible(
    const psx_semantic_type_table_t *table,
    psx_qual_type_t left, psx_qual_type_t right,
    int compare_qualifiers);

static int semantic_integer_shapes_compatible(
    const psx_type_shape_t *left,
    const psx_type_shape_t *right) {
  if (!left || !right || left->kind != PSX_TYPE_INTEGER ||
      right->kind != PSX_TYPE_INTEGER)
    return 0;
  int left_is_enum =
      left->integer_kind == PSX_INTEGER_KIND_ENUM;
  int right_is_enum =
      right->integer_kind == PSX_INTEGER_KIND_ENUM;
  if (!left_is_enum && !right_is_enum)
    return semantic_type_shapes_match(left, right);
  if (left_is_enum && right_is_enum)
    return semantic_type_shapes_match(left, right);
  const psx_type_shape_t *enumeration =
      left_is_enum ? left : right;
  const psx_type_shape_t *integer =
      left_is_enum ? right : left;
  psx_integer_conversion_t conversion =
      psx_integer_conversion_from_shape(integer);
  return conversion.is_integer && conversion.rank == 3 &&
         !integer->is_plain_char &&
         conversion.is_unsigned == enumeration->is_unsigned;
}

static int parameter_is_unchanged_by_default_argument_promotions(
    const psx_semantic_type_table_t *table, psx_qual_type_t parameter) {
  if (!semantic_type_id_is_valid(table, parameter.type_id)) return 0;
  const psx_type_shape_t *shape = &table->entries[parameter.type_id].shape;
  switch (shape->kind) {
    case PSX_TYPE_INTEGER:
      return shape->integer_kind >= PSX_INTEGER_KIND_INT;
    case PSX_TYPE_FLOAT:
      return shape->floating_kind != PSX_FLOATING_KIND_FLOAT;
    case PSX_TYPE_COMPLEX:
      return 1;
    case PSX_TYPE_POINTER:
    case PSX_TYPE_STRUCT:
    case PSX_TYPE_UNION:
      return 1;
    default:
      return 0;
  }
}

static int semantic_function_entries_compatible(
    const psx_semantic_type_table_t *table,
    const psx_semantic_type_entry_t *left_entry,
    const psx_semantic_type_entry_t *right_entry) {
  const psx_type_shape_t *left_shape = &left_entry->shape;
  const psx_type_shape_t *right_shape = &right_entry->shape;
  if (!semantic_qual_types_compatible(
          table, left_entry->base_type, right_entry->base_type, 1))
    return 0;

  if (left_shape->has_function_prototype &&
      right_shape->has_function_prototype) {
    if (left_entry->parameter_count != right_entry->parameter_count ||
        left_shape->is_variadic_function !=
            right_shape->is_variadic_function)
      return 0;
    for (int i = 0; i < left_entry->parameter_count; i++) {
      if (!semantic_qual_types_compatible(
              table, left_entry->parameter_types[i],
              right_entry->parameter_types[i], 1))
        return 0;
    }
    return 1;
  }

  if (!left_shape->has_function_prototype &&
      !right_shape->has_function_prototype) {
    if (left_entry->parameter_count == 0 ||
        right_entry->parameter_count == 0)
      return 1;
    if (left_entry->parameter_count != right_entry->parameter_count)
      return 0;
    for (int i = 0; i < left_entry->parameter_count; i++) {
      if (!semantic_qual_types_compatible(
              table, left_entry->parameter_types[i],
              right_entry->parameter_types[i], 1))
        return 0;
    }
    return 1;
  }

  const psx_semantic_type_entry_t *prototype =
      left_shape->has_function_prototype ? left_entry : right_entry;
  const psx_semantic_type_entry_t *old_style =
      left_shape->has_function_prototype ? right_entry : left_entry;
  if (prototype->shape.is_variadic_function) return 0;
  if (old_style->parameter_count > 0 &&
      old_style->parameter_count != prototype->parameter_count)
    return 0;
  for (int i = 0; i < prototype->parameter_count; i++) {
    if (!parameter_is_unchanged_by_default_argument_promotions(
            table, prototype->parameter_types[i]))
      return 0;
    if (old_style->parameter_count > 0 &&
        !semantic_qual_types_compatible(
            table, prototype->parameter_types[i],
            old_style->parameter_types[i], 1))
      return 0;
  }
  return 1;
}

static int semantic_qual_types_compatible(
    const psx_semantic_type_table_t *table,
    psx_qual_type_t left, psx_qual_type_t right,
    int compare_qualifiers) {
  if (!psx_semantic_type_table_qual_type_is_valid(table, left) ||
      !psx_semantic_type_table_qual_type_is_valid(table, right))
    return 0;
  if (compare_qualifiers && left.qualifiers != right.qualifiers) return 0;
  if (left.type_id == right.type_id) return 1;

  const psx_semantic_type_entry_t *left_entry =
      &table->entries[left.type_id];
  const psx_semantic_type_entry_t *right_entry =
      &table->entries[right.type_id];
  const psx_type_shape_t *left_shape = &left_entry->shape;
  const psx_type_shape_t *right_shape = &right_entry->shape;
  if (left_shape->kind != right_shape->kind) return 0;

  switch (left_shape->kind) {
    case PSX_TYPE_INTEGER:
      return semantic_integer_shapes_compatible(
          left_shape, right_shape);
    case PSX_TYPE_POINTER:
      return semantic_qual_types_compatible(
          table, left_entry->base_type, right_entry->base_type, 1);
    case PSX_TYPE_ARRAY: {
      int left_has_constant_bound =
          !left_shape->is_vla && left_shape->array_len > 0;
      int right_has_constant_bound =
          !right_shape->is_vla && right_shape->array_len > 0;
      if (left_has_constant_bound && right_has_constant_bound &&
          left_shape->array_len != right_shape->array_len)
        return 0;
      return semantic_qual_types_compatible(
          table, left_entry->base_type, right_entry->base_type, 1);
    }
    case PSX_TYPE_FUNCTION:
      return semantic_function_entries_compatible(
          table, left_entry, right_entry);
    default:
      return semantic_type_shapes_match(left_shape, right_shape);
  }
}

int psx_semantic_type_table_types_compatible(
    const psx_semantic_type_table_t *table,
    psx_qual_type_t left, psx_qual_type_t right) {
  return semantic_qual_types_compatible(table, left, right, 1);
}

static psx_qual_type_t semantic_composite_qual_types(
    psx_semantic_type_table_t *table,
    psx_qual_type_t left, psx_qual_type_t right) {
  if (!semantic_qual_types_compatible(table, left, right, 1))
    return invalid_qual_type();
  if (left.type_id == right.type_id) return left;

  psx_semantic_type_entry_t left_entry =
      table->entries[left.type_id];
  psx_semantic_type_entry_t right_entry =
      table->entries[right.type_id];
  const psx_type_shape_t left_shape = left_entry.shape;
  const psx_type_shape_t right_shape = right_entry.shape;
  psx_qual_type_t composite = invalid_qual_type();

  switch (left_shape.kind) {
    case PSX_TYPE_POINTER: {
      psx_qual_type_t pointee = semantic_composite_qual_types(
          table, left_entry.base_type, right_entry.base_type);
      if (pointee.type_id == PSX_TYPE_ID_INVALID)
        return invalid_qual_type();
      composite =
          psx_semantic_type_table_intern_pointer_to(table, pointee);
      break;
    }
    case PSX_TYPE_ARRAY: {
      psx_qual_type_t element = semantic_composite_qual_types(
          table, left_entry.base_type, right_entry.base_type);
      if (element.type_id == PSX_TYPE_ID_INVALID)
        return invalid_qual_type();
      int array_len = 0;
      int is_vla = 0;
      if (!left_shape.is_vla && left_shape.array_len > 0) {
        array_len = left_shape.array_len;
      } else if (!right_shape.is_vla &&
                 right_shape.array_len > 0) {
        array_len = right_shape.array_len;
      } else if (left_shape.is_vla || right_shape.is_vla) {
        is_vla = 1;
      }
      composite = psx_semantic_type_table_intern_array_of(
          table, element, array_len, is_vla);
      break;
    }
    case PSX_TYPE_FUNCTION: {
      psx_qual_type_t result = semantic_composite_qual_types(
          table, left_entry.base_type, right_entry.base_type);
      if (result.type_id == PSX_TYPE_ID_INVALID)
        return invalid_qual_type();
      const psx_semantic_type_entry_t *prototype =
          left_shape.has_function_prototype
              ? &left_entry
              : right_shape.has_function_prototype
                    ? &right_entry
                    : NULL;
      int parameter_count = prototype
                                ? prototype->parameter_count
                                : left_entry.parameter_count > 0
                                      ? left_entry.parameter_count
                                      : right_entry.parameter_count;
      psx_qual_type_t *parameters = NULL;
      if (parameter_count > 0) {
        parameters = malloc(
            (size_t)parameter_count * sizeof(*parameters));
        if (!parameters) return invalid_qual_type();
      }
      for (int i = 0; i < parameter_count; i++) {
        int has_left = i < left_entry.parameter_count;
        int has_right = i < right_entry.parameter_count;
        if (has_left && has_right) {
          parameters[i] = semantic_composite_qual_types(
              table, left_entry.parameter_types[i],
              right_entry.parameter_types[i]);
        } else {
          parameters[i] = has_left
                              ? left_entry.parameter_types[i]
                              : right_entry.parameter_types[i];
        }
        if (parameters[i].type_id == PSX_TYPE_ID_INVALID) {
          free(parameters);
          return invalid_qual_type();
        }
      }
      int has_prototype = prototype != NULL;
      int is_variadic =
          prototype ? prototype->shape.is_variadic_function : 0;
      composite = psx_semantic_type_table_intern_function(
          table, result, parameters, parameter_count,
          has_prototype, is_variadic);
      free(parameters);
      break;
    }
    default:
      composite = left;
      break;
  }
  if (composite.type_id != PSX_TYPE_ID_INVALID)
    composite.qualifiers = left.qualifiers;
  return composite;
}

psx_qual_type_t psx_semantic_type_table_composite_type(
    psx_semantic_type_table_t *table,
    psx_qual_type_t left, psx_qual_type_t right) {
  return semantic_composite_qual_types(table, left, right);
}

int psx_semantic_type_table_function_types_compatible(
    const psx_semantic_type_table_t *table,
    psx_qual_type_t left, psx_qual_type_t right) {
  psx_type_shape_t left_shape = {0};
  psx_type_shape_t right_shape = {0};
  return psx_semantic_type_table_describe(
             table, left.type_id, &left_shape) &&
         psx_semantic_type_table_describe(
             table, right.type_id, &right_shape) &&
         left_shape.kind == PSX_TYPE_FUNCTION &&
         right_shape.kind == PSX_TYPE_FUNCTION &&
         semantic_qual_types_compatible(table, left, right, 1);
}

static psx_qual_type_t related_type(
    const psx_semantic_type_table_t *table, psx_qual_type_t relation) {
  if (!semantic_type_id_is_valid(table, relation.type_id))
    return invalid_qual_type();
  return relation;
}

psx_qual_type_t psx_semantic_type_table_base(
    const psx_semantic_type_table_t *table, psx_type_id_t type_id) {
  if (!semantic_type_id_is_valid(table, type_id))
    return invalid_qual_type();
  return related_type(table, table->entries[type_id].base_type);
}

int psx_semantic_type_table_contains_vla_array(
    const psx_semantic_type_table_t *table, psx_type_id_t type_id) {
  for (psx_type_id_t current = type_id;
       semantic_type_id_is_valid(table, current);) {
    const psx_type_shape_t *shape = &table->entries[current].shape;
    if (shape->kind == PSX_TYPE_ARRAY && shape->is_vla) return 1;
    psx_qual_type_t base = psx_semantic_type_table_base(table, current);
    if (base.type_id == PSX_TYPE_ID_INVALID) return 0;
    current = base.type_id;
  }
  return 0;
}

static psx_qual_type_t semantic_type_table_array_leaf_from(
    const psx_semantic_type_table_t *table, psx_qual_type_t result) {
  result = related_type(table, result);
  while (semantic_type_id_is_valid(table, result.type_id) &&
         table->entries[result.type_id].shape.kind == PSX_TYPE_ARRAY) {
    result = psx_semantic_type_table_base(table, result.type_id);
  }
  return semantic_type_id_is_valid(table, result.type_id)
             ? result : invalid_qual_type();
}

psx_qual_type_t psx_semantic_type_table_array_leaf(
    const psx_semantic_type_table_t *table, psx_type_id_t type_id) {
  return semantic_type_table_array_leaf_from(
      table, (psx_qual_type_t){type_id, PSX_TYPE_QUALIFIER_NONE});
}

int psx_semantic_type_table_array_flat_element_count(
    const psx_semantic_type_table_t *table, psx_type_id_t type_id) {
  psx_type_shape_t shape = {0};
  if (!psx_semantic_type_table_describe(table, type_id, &shape) ||
      shape.kind != PSX_TYPE_ARRAY)
    return 0;
  int count = 1;
  while (shape.kind == PSX_TYPE_ARRAY) {
    if (shape.array_len <= 0 || count > INT_MAX / shape.array_len)
      return 0;
    count *= shape.array_len;
    psx_qual_type_t base = psx_semantic_type_table_base(table, type_id);
    if (base.type_id == PSX_TYPE_ID_INVALID ||
        !psx_semantic_type_table_describe(table, base.type_id, &shape))
      return 0;
    type_id = base.type_id;
  }
  return count;
}

int psx_semantic_type_table_array_subscript_stride_elements(
    const psx_semantic_type_table_t *table, psx_type_id_t type_id,
    int depth) {
  psx_type_shape_t shape = {0};
  if (depth < 0 ||
      !psx_semantic_type_table_describe(table, type_id, &shape) ||
      shape.kind != PSX_TYPE_ARRAY)
    return 0;
  while (depth-- > 0) {
    psx_qual_type_t base = psx_semantic_type_table_base(table, type_id);
    if (base.type_id == PSX_TYPE_ID_INVALID ||
        !psx_semantic_type_table_describe(table, base.type_id, &shape) ||
        shape.kind != PSX_TYPE_ARRAY)
      return 0;
    type_id = base.type_id;
  }
  psx_qual_type_t selected = psx_semantic_type_table_base(table, type_id);
  if (selected.type_id == PSX_TYPE_ID_INVALID ||
      !psx_semantic_type_table_describe(table, selected.type_id, &shape))
    return 0;
  return shape.kind == PSX_TYPE_ARRAY
             ? psx_semantic_type_table_array_flat_element_count(
                   table, selected.type_id)
             : 1;
}

psx_qual_type_t psx_semantic_type_table_pointee_value(
    const psx_semantic_type_table_t *table, psx_type_id_t type_id) {
  psx_type_shape_t type = {0};
  if (!psx_semantic_type_table_describe(table, type_id, &type) ||
      (type.kind != PSX_TYPE_POINTER && type.kind != PSX_TYPE_ARRAY)) {
    return invalid_qual_type();
  }
  psx_qual_type_t base = psx_semantic_type_table_base(table, type_id);
  return semantic_type_table_array_leaf_from(table, base);
}

psx_qual_type_t psx_semantic_type_table_callable_function(
    const psx_semantic_type_table_t *table, psx_qual_type_t type) {
  psx_type_shape_t current = {0};
  int has_current = psx_semantic_type_table_describe(
      table, type.type_id, &current);
  if (has_current && current.kind == PSX_TYPE_POINTER) {
    type = psx_semantic_type_table_base(table, type.type_id);
    has_current = psx_semantic_type_table_describe(
        table, type.type_id, &current);
  }
  return has_current && current.kind == PSX_TYPE_FUNCTION
             ? type : invalid_qual_type();
}

int psx_semantic_type_is_exact_int_void_function(
    const psx_semantic_type_table_t *table, psx_qual_type_t type) {
  psx_qual_type_t function_type =
      psx_semantic_type_table_callable_function(table, type);
  psx_qual_type_t result_type = psx_semantic_type_table_base(
      table, function_type.type_id);
  psx_type_shape_t function = {0};
  psx_type_shape_t result = {0};
  return psx_semantic_type_table_describe(
             table, function_type.type_id, &function) &&
         psx_semantic_type_table_describe(
             table, result_type.type_id, &result) &&
         function.parameter_count == 0 &&
         !function.is_variadic_function &&
         result.kind == PSX_TYPE_INTEGER &&
         result.integer_kind == PSX_INTEGER_KIND_INT &&
         !result.is_unsigned;
}

psx_qual_type_t psx_semantic_type_table_aggregate_object(
    const psx_semantic_type_table_t *table, psx_qual_type_t type) {
  while (type.type_id != PSX_TYPE_ID_INVALID) {
    psx_type_shape_t current = {0};
    if (!psx_semantic_type_table_describe(
            table, type.type_id, &current))
      return invalid_qual_type();
    if (current.kind == PSX_TYPE_STRUCT || current.kind == PSX_TYPE_UNION)
      return type;
    if (current.kind != PSX_TYPE_POINTER &&
        current.kind != PSX_TYPE_ARRAY)
      return invalid_qual_type();
    type = psx_semantic_type_table_base(table, type.type_id);
  }
  return invalid_qual_type();
}

psx_qual_type_t psx_semantic_type_table_parameter(
    const psx_semantic_type_table_t *table, psx_type_id_t type_id,
    int parameter_index) {
  if (!semantic_type_id_is_valid(table, type_id) || parameter_index < 0 ||
      parameter_index >= table->entries[type_id].parameter_count ||
      !table->entries[type_id].parameter_types)
    return invalid_qual_type();
  return related_type(
      table, table->entries[type_id].parameter_types[parameter_index]);
}

psx_qual_type_t psx_semantic_type_table_record_member(
    const psx_semantic_type_table_t *table, psx_type_id_t type_id,
    int member_index) {
  psx_type_shape_t type = {0};
  if (!psx_semantic_type_table_describe(table, type_id, &type) ||
      (type.kind != PSX_TYPE_STRUCT && type.kind != PSX_TYPE_UNION))
    return invalid_qual_type();
  const psx_record_decl_t *record = psx_record_decl_table_lookup(
      table->record_decls, type.record_id);
  if (!record || !record->members || member_index < 0 ||
      member_index >= record->member_count)
    return invalid_qual_type();
  return related_type(table, record->members[member_index].decl_qual_type);
}
