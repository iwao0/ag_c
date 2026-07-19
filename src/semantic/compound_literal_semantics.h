#ifndef SEMANTIC_COMPOUND_LITERAL_SEMANTICS_H
#define SEMANTIC_COMPOUND_LITERAL_SEMANTICS_H

#include "type_identity.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;

typedef enum {
  PSX_COMPOUND_LITERAL_STORAGE_AUTOMATIC = 0,
  PSX_COMPOUND_LITERAL_STORAGE_STATIC,
} psx_compound_literal_storage_duration_t;

typedef struct {
  psx_compound_literal_storage_duration_t storage_duration;
  psx_qual_type_t object_qual_type;
} psx_compound_literal_plan_t;

int psx_resolve_compound_literal_qual_type_plan_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t object_qual_type,
    int has_file_scope_storage,
    psx_compound_literal_plan_t *plan);

#endif
