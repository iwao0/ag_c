#ifndef CODEGEN_BACKEND_H
#define CODEGEN_BACKEND_H

#include <stddef.h>

struct node_t;
typedef void (*gen_output_line_fn)(const char *line, size_t len, void *user_data);

// コード生成関数 (arch/ 以下で実装)
void gen_main_prologue(void);
void gen_main_epilogue(void);
void gen(struct node_t *node);
void gen_string_literals(void);
void gen_float_literals(void);
void gen_global_vars(void);
void gen_set_output_callback(gen_output_line_fn cb, void *user_data);

#endif
