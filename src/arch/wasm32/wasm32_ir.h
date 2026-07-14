#ifndef AG_WASM32_IR_H
#define AG_WASM32_IR_H

#include "../../ir/ir.h"
#include "../../ir/ir_data.h"

void wasm32_module_begin(void);
void wasm32_gen_ir_module(ir_module_t *m);
void wasm32_emit_data_segments(const ir_data_module_t *data_module);
void wasm32_module_end(void);

#endif /* AG_WASM32_IR_H */
