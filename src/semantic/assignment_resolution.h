#ifndef SEMANTIC_ASSIGNMENT_RESOLUTION_H
#define SEMANTIC_ASSIGNMENT_RESOLUTION_H

#include "../type_system/type_ids.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;

typedef enum {
  PSX_ASSIGNMENT_TYPES_OK = 0,
  PSX_ASSIGNMENT_TYPES_INVALID,
  PSX_ASSIGNMENT_TARGET_NOT_MODIFIABLE,
  PSX_ASSIGNMENT_TYPES_INCOMPATIBLE,
  PSX_ASSIGNMENT_DISCARDS_QUALIFIERS,
} psx_assignment_types_status_t;

typedef struct {
  psx_assignment_types_status_t status;
  psx_qual_type_t result_qual_type;
} psx_assignment_types_resolution_t;

typedef enum {
  PSX_COMPOUND_ASSIGN_ADD = 0,
  PSX_COMPOUND_ASSIGN_SUB,
  PSX_COMPOUND_ASSIGN_MUL,
  PSX_COMPOUND_ASSIGN_DIV,
  PSX_COMPOUND_ASSIGN_MOD,
  PSX_COMPOUND_ASSIGN_SHL,
  PSX_COMPOUND_ASSIGN_SHR,
  PSX_COMPOUND_ASSIGN_BITAND,
  PSX_COMPOUND_ASSIGN_BITXOR,
  PSX_COMPOUND_ASSIGN_BITOR,
} psx_compound_assignment_operator_t;

void psx_resolve_assignment_qual_types_in(
    const psx_semantic_context_t *semantic_context,
    psx_qual_type_t target_type,
    psx_qual_type_t value_type,
    int value_is_null_pointer_constant,
    psx_assignment_types_resolution_t *resolution);

void psx_resolve_compound_assignment_qual_types_in(
    const psx_semantic_context_t *semantic_context,
    psx_compound_assignment_operator_t operation,
    psx_qual_type_t target_type,
    psx_qual_type_t value_type,
    psx_assignment_types_resolution_t *resolution);

#endif
