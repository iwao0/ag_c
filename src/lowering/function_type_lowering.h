#ifndef AG_FUNCTION_TYPE_LOWERING_H
#define AG_FUNCTION_TYPE_LOWERING_H

#include "../ir/ir.h"

typedef struct psx_semantic_type_table_t psx_semantic_type_table_t;

int ir_function_type_from_type_id(
    const psx_semantic_type_table_t *semantic_types,
    psx_type_id_t type_id, ir_function_type_t *out);
int ir_function_type_format_c_signature(
    const psx_semantic_type_table_t *semantic_types,
    const ir_function_type_t *function_type,
    const ag_data_layout_t *data_layout,
    char **out, int *out_length);

#endif
