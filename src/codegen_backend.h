#ifndef CODEGEN_BACKEND_H
#define CODEGEN_BACKEND_H

#include <stddef.h>
#include "ir/ir_data.h"

typedef void (*gen_output_line_fn)(const char *line, size_t len, void *user_data);

/* Backend data-section emitters and shared output routing.
 * Function bodies are emitted through the IR backend. */
void gen_string_literals(const ir_data_module_t *data_module);
void gen_float_literals(const ir_data_module_t *data_module);
void gen_global_vars(void);
void gen_set_output_callback(gen_output_line_fn cb, void *user_data);
void gen_set_simple_formatter(int enable);

#endif
