#ifndef AG_WASM32_IR_H
#define AG_WASM32_IR_H

#include "wasm32_machine_module.h"

typedef struct ir_abi_data_module_t ir_abi_data_module_t;
#include "../../ir/ir_data.h"

typedef struct wasm32_ir_context_t wasm32_ir_context_t;
typedef struct ag_codegen_emit_context_t ag_codegen_emit_context_t;

wasm32_ir_context_t *wasm32_ir_context_create(
    ag_codegen_emit_context_t *emit_context);
void wasm32_ir_context_destroy(wasm32_ir_context_t *ctx);

void wasm32_module_begin_in(wasm32_ir_context_t *ctx);
void wasm32_gen_machine_module_in(
    wasm32_ir_context_t *ctx,
    const wasm32_machine_module_t *machine_module);
void wasm32_emit_data_segments_in(
    wasm32_ir_context_t *ctx,
    const ir_data_module_t *data_module,
    const ir_abi_data_module_t *data_abi);
void wasm32_module_end_in(wasm32_ir_context_t *ctx);

#endif /* AG_WASM32_IR_H */
