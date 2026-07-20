#ifndef SEMANTIC_TYPE_IDENTITY_INTERNAL_H
#define SEMANTIC_TYPE_IDENTITY_INTERNAL_H

#include "type_identity.h"
#include "type_compatibility_cache_internal.h"

/* Structural core boundary: no parser type or target layout enters here. */
psx_qual_type_t psx_semantic_type_table_find_shape(
    const psx_semantic_type_table_t *table,
    const psx_type_shape_t *shape, psx_qual_type_t base_type,
    const psx_qual_type_t *parameter_types, int parameter_count);
psx_qual_type_t psx_semantic_type_table_intern_shape(
    psx_semantic_type_table_t *table,
    const psx_type_shape_t *shape, psx_qual_type_t base_type,
    const psx_qual_type_t *parameter_types, int parameter_count);
psx_type_compatibility_cache_t *
psx_semantic_type_table_compatibility_cache(
    const psx_semantic_type_table_t *table);

#endif
