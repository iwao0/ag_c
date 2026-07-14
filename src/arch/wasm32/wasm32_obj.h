#ifndef AG_WASM32_OBJ_H
#define AG_WASM32_OBJ_H

#include "../../ir/ir.h"
#include <stdio.h>

void wasm32_obj_set_output_file(FILE *out);
void wasm32_obj_capture_output(int enabled);
void wasm32_obj_set_capture_limit(size_t max_bytes);
int wasm32_obj_capture_limit_exceeded(void);
unsigned char *wasm32_obj_take_output(size_t *out_len);
void wasm32_obj_begin(void);
void wasm32_obj_gen_ir_module(ir_module_t *m);
void wasm32_obj_emit_data_segments(void);
void wasm32_obj_end(void);

#endif /* AG_WASM32_OBJ_H */
