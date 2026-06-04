#ifndef CODEGEN_BACKEND_H
#define CODEGEN_BACKEND_H

#include <stddef.h>

typedef void (*gen_output_line_fn)(const char *line, size_t len, void *user_data);

/* arch/arm64_apple.c が提供する共有ヘルパ。
 * 関数本体の codegen は IR 経路 (arm64_apple_ir.c の gen_ir_module) で行う。 */
void gen_string_literals(void);
void gen_float_literals(void);
void gen_global_vars(void);
void gen_set_output_callback(gen_output_line_fn cb, void *user_data);

#endif
