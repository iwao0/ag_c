#ifndef AG_IR_SYMBOL_LOWERING_H
#define AG_IR_SYMBOL_LOWERING_H

#include "../ir/ir.h"

typedef struct global_var_t global_var_t;

ir_symbol_t *lower_ir_global_symbol(
    ir_module_t *module, global_var_t *global);

#endif
