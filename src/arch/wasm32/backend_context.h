#ifndef AG_WASM32_BACKEND_CONTEXT_H
#define AG_WASM32_BACKEND_CONTEXT_H

#include "wasm32_ir.h"
#include "wasm32_obj.h"

typedef struct wasm32_backend_context_t wasm32_backend_context_t;
typedef struct ag_codegen_emit_context_t ag_codegen_emit_context_t;

wasm32_backend_context_t *wasm32_backend_context_create(
    ag_codegen_emit_context_t *emit_context);
void wasm32_backend_context_destroy(void *context);

void wasm32_backend_wat_begin(wasm32_backend_context_t *ctx);
void wasm32_backend_wat_gen_ir_module(
    wasm32_backend_context_t *ctx, ir_module_t *module,
    const ir_abi_module_t *abi);
void wasm32_backend_wat_emit_data_segments(
    wasm32_backend_context_t *ctx,
    const ir_data_module_t *data_module,
    const ir_abi_data_module_t *data_abi);
void wasm32_backend_wat_end(wasm32_backend_context_t *ctx);

void wasm32_backend_obj_set_output_file(
    wasm32_backend_context_t *ctx, FILE *out);
void wasm32_backend_obj_capture_output(
    wasm32_backend_context_t *ctx, int enabled);
void wasm32_backend_obj_set_capture_limit(
    wasm32_backend_context_t *ctx, size_t max_bytes);
int wasm32_backend_obj_capture_limit_exceeded(
    wasm32_backend_context_t *ctx);
unsigned char *wasm32_backend_obj_take_output(
    wasm32_backend_context_t *ctx, size_t *out_len);
void wasm32_backend_obj_begin(wasm32_backend_context_t *ctx);
void wasm32_backend_obj_gen_ir_module(
    wasm32_backend_context_t *ctx, ir_module_t *module,
    const ir_abi_module_t *abi);
void wasm32_backend_obj_emit_data_segments(
    wasm32_backend_context_t *ctx,
    const ir_data_module_t *data_module,
    const ir_abi_data_module_t *data_abi);
void wasm32_backend_obj_end(wasm32_backend_context_t *ctx);

#endif
