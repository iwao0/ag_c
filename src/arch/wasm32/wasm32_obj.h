#ifndef AG_WASM32_OBJ_H
#define AG_WASM32_OBJ_H

#include "../../ir/ir.h"

typedef struct ir_abi_module_t ir_abi_module_t;
typedef struct ir_abi_data_module_t ir_abi_data_module_t;
#include "../../ir/ir_data.h"
#include <stdio.h>

typedef struct wasm32_obj_context_t wasm32_obj_context_t;
typedef struct ag_diagnostic_context_t ag_diagnostic_context_t;

wasm32_obj_context_t *wasm32_obj_context_create(
    ag_diagnostic_context_t *diagnostic_context);
void wasm32_obj_context_destroy(wasm32_obj_context_t *ctx);

void wasm32_obj_set_output_file_in(wasm32_obj_context_t *ctx, FILE *out);
void wasm32_obj_capture_output_in(
    wasm32_obj_context_t *ctx, int enabled);
void wasm32_obj_set_capture_limit_in(
    wasm32_obj_context_t *ctx, size_t max_bytes);
int wasm32_obj_capture_limit_exceeded_in(wasm32_obj_context_t *ctx);
unsigned char *wasm32_obj_take_output_in(
    wasm32_obj_context_t *ctx, size_t *out_len);
void wasm32_obj_begin_in(wasm32_obj_context_t *ctx);
void wasm32_obj_gen_ir_module_in(
    wasm32_obj_context_t *ctx, ir_module_t *m,
    const ir_abi_module_t *abi);
void wasm32_obj_emit_data_segments_in(
    wasm32_obj_context_t *ctx,
    const ir_data_module_t *data_module,
    const ir_abi_data_module_t *data_abi);
void wasm32_obj_end_in(wasm32_obj_context_t *ctx);

#endif /* AG_WASM32_OBJ_H */
