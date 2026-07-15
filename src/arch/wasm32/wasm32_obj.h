#ifndef AG_WASM32_OBJ_H
#define AG_WASM32_OBJ_H

#include "../../ir/ir.h"
#include "../../ir/ir_data.h"
#include <stdio.h>

typedef struct wasm32_obj_context_t wasm32_obj_context_t;
typedef struct ag_diagnostic_context_t ag_diagnostic_context_t;

wasm32_obj_context_t *wasm32_obj_context_create(
    ag_diagnostic_context_t *diagnostic_context);
void wasm32_obj_context_destroy(wasm32_obj_context_t *ctx);
wasm32_obj_context_t *wasm32_obj_context_activate(
    wasm32_obj_context_t *ctx);
wasm32_obj_context_t *wasm32_obj_context_active(void);

void wasm32_obj_set_output_file(FILE *out);
void wasm32_obj_capture_output(int enabled);
void wasm32_obj_set_capture_limit(size_t max_bytes);
int wasm32_obj_capture_limit_exceeded(void);
unsigned char *wasm32_obj_take_output(size_t *out_len);
void wasm32_obj_begin(void);
void wasm32_obj_gen_ir_module(ir_module_t *m);
void wasm32_obj_emit_data_segments(const ir_data_module_t *data_module);
void wasm32_obj_end(void);

#endif /* AG_WASM32_OBJ_H */
