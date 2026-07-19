#ifndef AG_TYPE_SIGNATURE_H
#define AG_TYPE_SIGNATURE_H

#include <stddef.h>

#include "semantic/type_identity.h"

struct ag_target_info_t;

int psx_format_canonical_type_signature(
    const psx_semantic_type_table_t *types, psx_qual_type_t type,
    const struct ag_target_info_t *target, char *out, size_t out_size);

#endif
