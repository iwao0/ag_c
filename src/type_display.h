#ifndef AG_TYPE_DISPLAY_H
#define AG_TYPE_DISPLAY_H

#include <stddef.h>

#include "type_system/type_ids.h"

typedef struct psx_semantic_type_table_t psx_semantic_type_table_t;

/* Formats an interned semantic type as a bounded, human-readable C type.
 * The returned length excludes the trailing NUL. A zero-sized output queries
 * the required size. */
int ag_format_c_type(const psx_semantic_type_table_t *types,
                     psx_qual_type_t type, char *out, size_t out_size);

#endif
