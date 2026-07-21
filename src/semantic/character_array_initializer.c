#include "character_array_initializer.h"

#include <limits.h>
#include <string.h>

#include "../parser/arena.h"
#include "../tokenizer/literals.h"
#include "../type_layout.h"
#include "type_identity.h"

psx_character_array_initializer_status_t
psx_resolve_character_array_string_shape(
    int array_capacity, int element_width,
    const char *literal_contents, int literal_length, int character_width,
    psx_character_array_string_shape_t *shape) {
  if (shape) *shape = (psx_character_array_string_shape_t){0};
  if (array_capacity < 0 || !literal_contents || literal_length < 0 ||
      !shape)
    return PSX_CHARACTER_ARRAY_INITIALIZER_INVALID;
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
    const ag_data_layout_t *data_layout,
    psx_qual_type_t object_qual_type,
    const char *literal_contents, int literal_length,
    int character_width,
    psx_character_array_initializer_plan_t *plan) {
  if (plan) *plan = (psx_character_array_initializer_plan_t){0};
  if (!arena_context || !semantic_types ||
      !ag_data_layout_is_valid(data_layout) || !plan ||
      object_qual_type.type_id == PSX_TYPE_ID_INVALID)
    return PSX_CHARACTER_ARRAY_INITIALIZER_INVALID;
  psx_type_shape_t object_shape = {0};
  if (!psx_semantic_type_table_describe(
          semantic_types, object_qual_type.type_id, &object_shape) ||
      object_shape.kind != PSX_TYPE_ARRAY)
    return PSX_CHARACTER_ARRAY_INITIALIZER_NOT_CHARACTER_ARRAY;
  psx_qual_type_t element_qual_type = psx_semantic_type_table_base(
      semantic_types, object_qual_type.type_id);
  psx_type_shape_t element_shape = {0};
  if (element_qual_type.type_id == PSX_TYPE_ID_INVALID ||
      !psx_semantic_type_table_describe(
          semantic_types, element_qual_type.type_id, &element_shape))
    return PSX_CHARACTER_ARRAY_INITIALIZER_INVALID;
  psx_character_array_string_shape_t shape;
  psx_character_array_initializer_status_t status =
      psx_resolve_character_array_string_shape(
          object_shape.array_len,
          psx_type_layout_character_code_unit_width(
              semantic_types, element_qual_type.type_id, data_layout),
          literal_contents, literal_length,
          character_width, &shape);
  if (status != PSX_CHARACTER_ARRAY_INITIALIZER_OK) return status;
  if (object_shape.array_len <= 0)
    return PSX_CHARACTER_ARRAY_INITIALIZER_INVALID;
  if (shape.content_unit_count > object_shape.array_len)
    return PSX_CHARACTER_ARRAY_INITIALIZER_TOO_LONG;
  uint32_t *units = arena_alloc_in(
      arena_context,
      (size_t)object_shape.array_len * sizeof(*units));
  if (!units) return PSX_CHARACTER_ARRAY_INITIALIZER_OUT_OF_MEMORY;
  memset(units, 0,
         (size_t)object_shape.array_len * sizeof(*units));
  character_array_unit_writer_t writer = {
      .units = units,
      .capacity = object_shape.array_len,
  };
  int emitted = tk_emit_string_code_units(
      literal_contents, literal_length, shape.character_width,
      object_shape.array_len, write_character_array_unit, &writer);
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
