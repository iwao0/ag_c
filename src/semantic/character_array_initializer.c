#include "character_array_initializer.h"

#include <limits.h>
#include <string.h>

#include "../parser/arena.h"
#include "../parser/type.h"
#include "../tokenizer/literals.h"
#include "type_identity.h"

psx_character_array_initializer_status_t
psx_resolve_character_array_string_shape(
    const psx_type_t *array_type, const char *literal_contents,
    int literal_length, int character_width,
    psx_character_array_string_shape_t *shape) {
  if (shape) *shape = (psx_character_array_string_shape_t){0};
  if (!array_type || !literal_contents || literal_length < 0 ||
      !shape)
    return PSX_CHARACTER_ARRAY_INITIALIZER_INVALID;
  if (array_type->kind != PSX_TYPE_ARRAY || !array_type->base)
    return PSX_CHARACTER_ARRAY_INITIALIZER_NOT_CHARACTER_ARRAY;
  int element_width =
      ps_type_character_code_unit_width(array_type->base);
  int literal_width = character_width > 0 ? character_width : 1;
  if (element_width <= 0)
    return PSX_CHARACTER_ARRAY_INITIALIZER_NOT_CHARACTER_ARRAY;
  if (element_width != literal_width)
    return PSX_CHARACTER_ARRAY_INITIALIZER_WIDTH_MISMATCH;
  int content_unit_count = tk_count_string_code_units(
      literal_contents, literal_length, literal_width);
  if (content_unit_count < 0 || content_unit_count == INT_MAX)
    return PSX_CHARACTER_ARRAY_INITIALIZER_TOO_LONG;
  *shape = (psx_character_array_string_shape_t){
      .content_unit_count = content_unit_count,
      .inferred_capacity = content_unit_count + 1,
      .character_width = literal_width,
  };
  return PSX_CHARACTER_ARRAY_INITIALIZER_OK;
}

typedef struct {
  uint32_t *units;
  int count;
  int capacity;
} character_array_unit_writer_t;

static void write_character_array_unit(uint32_t unit, void *opaque) {
  character_array_unit_writer_t *writer = opaque;
  if (!writer || writer->count >= writer->capacity) return;
  writer->units[writer->count++] = unit;
}

psx_character_array_initializer_status_t
psx_plan_character_array_string_initializer(
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types,
    psx_qual_type_t object_qual_type,
    const char *literal_contents, int literal_length,
    int character_width,
    psx_character_array_initializer_plan_t *plan) {
  if (plan) *plan = (psx_character_array_initializer_plan_t){0};
  if (!arena_context || !semantic_types || !plan ||
      object_qual_type.type_id == PSX_TYPE_ID_INVALID)
    return PSX_CHARACTER_ARRAY_INITIALIZER_INVALID;
  const psx_type_t *object_type = psx_semantic_type_table_lookup(
      semantic_types, object_qual_type.type_id);
  psx_character_array_string_shape_t shape;
  psx_character_array_initializer_status_t status =
      psx_resolve_character_array_string_shape(
          object_type, literal_contents, literal_length,
          character_width, &shape);
  if (status != PSX_CHARACTER_ARRAY_INITIALIZER_OK) return status;
  if (object_type->array_len <= 0)
    return PSX_CHARACTER_ARRAY_INITIALIZER_INVALID;
  if (shape.content_unit_count > object_type->array_len)
    return PSX_CHARACTER_ARRAY_INITIALIZER_TOO_LONG;
  psx_qual_type_t element_qual_type =
      psx_semantic_type_table_base(
          semantic_types, object_qual_type.type_id);
  if (element_qual_type.type_id == PSX_TYPE_ID_INVALID)
    return PSX_CHARACTER_ARRAY_INITIALIZER_INVALID;
  uint32_t *units = arena_alloc_in(
      arena_context,
      (size_t)object_type->array_len * sizeof(*units));
  if (!units) return PSX_CHARACTER_ARRAY_INITIALIZER_OUT_OF_MEMORY;
  memset(units, 0,
         (size_t)object_type->array_len * sizeof(*units));
  character_array_unit_writer_t writer = {
      .units = units,
      .capacity = object_type->array_len,
  };
  int emitted = tk_emit_string_code_units(
      literal_contents, literal_length, shape.character_width,
      object_type->array_len, write_character_array_unit, &writer);
  if (emitted != shape.content_unit_count ||
      writer.count != shape.content_unit_count)
    return PSX_CHARACTER_ARRAY_INITIALIZER_INVALID;
  if (writer.count < writer.capacity)
    units[writer.count++] = 0;
  while (writer.count < writer.capacity)
    units[writer.count++] = 0;
  *plan = (psx_character_array_initializer_plan_t){
      .object_qual_type = object_qual_type,
      .element_qual_type = element_qual_type,
      .units = units,
      .unit_count = writer.count,
      .character_width = shape.character_width,
  };
  return PSX_CHARACTER_ARRAY_INITIALIZER_OK;
}
