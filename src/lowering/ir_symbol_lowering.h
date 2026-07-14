#ifndef AG_IR_SYMBOL_LOWERING_H
#define AG_IR_SYMBOL_LOWERING_H

#include "../ir/ir.h"

ir_symbol_t *lower_ir_global_symbol(ir_module_t *module,
                                    const char *name, int name_len);

#endif
