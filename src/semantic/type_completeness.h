#ifndef SEMANTIC_TYPE_COMPLETENESS_H
#define SEMANTIC_TYPE_COMPLETENESS_H

#include "../type_system/type_ids.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;

int psx_semantic_type_is_complete_object_in(
    psx_semantic_context_t *semantic_context,
    psx_type_id_t type_id);

int psx_semantic_pointer_points_to_complete_object_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t pointer_type);

#endif
