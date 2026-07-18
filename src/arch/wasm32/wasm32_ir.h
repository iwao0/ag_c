#ifndef AG_WASM32_IR_H
#define AG_WASM32_IR_H

#include "../../ir/ir.h"

typedef struct ir_abi_module_t ir_abi_module_t;
#include "../../ir/ir_data.h"

typedef struct wasm32_ir_context_t wasm32_ir_context_t;
typedef struct ag_codegen_emit_context_t ag_codegen_emit_context_t;

wasm32_ir_context_t *wasm32_ir_context_create(
    ag_codegen_emit_context_t *emit_context);
void wasm32_ir_context_destroy(wasm32_ir_context_t *ctx);
wasm32_ir_context_t *wasm32_ir_context_activate(wasm32_ir_context_t *ctx);
wasm32_ir_context_t *wasm32_ir_context_active(void);

void wasm32_module_begin(void);
void wasm32_gen_ir_module(
    ir_module_t *m, const ir_abi_module_t *abi);
void wasm32_emit_data_segments(const ir_data_module_t *data_module);
void wasm32_module_end(void);

#endif /* AG_WASM32_IR_H */
