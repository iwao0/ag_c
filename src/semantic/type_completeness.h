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

int psx_semantic_record_contains_flexible_array_member_in(
    psx_semantic_context_t *semantic_context,
    psx_record_id_t record_id);

int psx_semantic_type_has_flexible_array_element_in(
    psx_semantic_context_t *semantic_context,
    psx_type_id_t type_id);

int psx_semantic_type_has_incomplete_array_element_in(
    psx_semantic_context_t *semantic_context,
    psx_type_id_t type_id);

int psx_semantic_type_has_invalid_atomic_qualification_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t type);

#endif
