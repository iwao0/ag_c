#ifndef AG_IR_SYMBOL_LOWERING_H
#define AG_IR_SYMBOL_LOWERING_H

#include "../ir/ir.h"

typedef struct global_var_t global_var_t;
typedef struct psx_semantic_type_table_t psx_semantic_type_table_t;
typedef struct psx_record_layout_table_t psx_record_layout_table_t;
typedef struct ag_target_info_t ag_target_info_t;

ir_symbol_t *lower_ir_global_symbol(
    ir_module_t *module, global_var_t *global,
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target);

#endif
