#ifndef AG_WASM32_OBJ_H
#define AG_WASM32_OBJ_H

#include "wasm32_machine_module.h"

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
void wasm32_obj_gen_machine_module_in(
    wasm32_obj_context_t *ctx,
    const wasm32_machine_module_t *machine_module);
void wasm32_obj_emit_machine_data_segments_in(
    wasm32_obj_context_t *ctx,
    const wasm32_machine_module_t *machine_module);
void wasm32_obj_end_in(wasm32_obj_context_t *ctx);

#endif /* AG_WASM32_OBJ_H */
