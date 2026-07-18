#ifndef SEMANTIC_CHARACTER_ARRAY_INITIALIZER_H
#define SEMANTIC_CHARACTER_ARRAY_INITIALIZER_H

#include <stdint.h>

#include "../type_system/type_ids.h"

typedef struct arena_context_t arena_context_t;
typedef struct psx_semantic_type_table_t psx_semantic_type_table_t;
typedef struct psx_type_t psx_type_t;

typedef enum {
  PSX_CHARACTER_ARRAY_INITIALIZER_OK = 0,
  PSX_CHARACTER_ARRAY_INITIALIZER_INVALID,
  PSX_CHARACTER_ARRAY_INITIALIZER_NOT_CHARACTER_ARRAY,
  PSX_CHARACTER_ARRAY_INITIALIZER_WIDTH_MISMATCH,
  PSX_CHARACTER_ARRAY_INITIALIZER_TOO_LONG,
  PSX_CHARACTER_ARRAY_INITIALIZER_OUT_OF_MEMORY,
} psx_character_array_initializer_status_t;

typedef struct {
  int content_unit_count;
  int inferred_capacity;
  int character_width;
} psx_character_array_string_shape_t;

typedef struct {
  psx_qual_type_t object_qual_type;
  psx_qual_type_t element_qual_type;
  const uint32_t *units;
  int unit_count;
  int character_width;
} psx_character_array_initializer_plan_t;

psx_character_array_initializer_status_t
psx_resolve_character_array_string_shape(
    const psx_type_t *array_type, const char *literal_contents,
    int literal_length, int character_width,
    psx_character_array_string_shape_t *shape);

psx_character_array_initializer_status_t
psx_plan_character_array_string_initializer(
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types,
    psx_qual_type_t object_qual_type,
    const char *literal_contents, int literal_length,
    int character_width,
    psx_character_array_initializer_plan_t *plan);

#endif
