#ifndef CODEGEN_BACKEND_H
#define CODEGEN_BACKEND_H

#include <stddef.h>
#include "ir/ir_data.h"

typedef void (*gen_output_line_fn)(const char *line, size_t len, void *user_data);
typedef struct ag_codegen_emit_context_t ag_codegen_emit_context_t;

/* Backend data-section emitters and shared output routing.
 * Function bodies are emitted through the IR backend. */
void gen_string_literals_in(
    ag_codegen_emit_context_t *emit_context,
    const ir_data_module_t *data_module);
void gen_float_literals_in(
    ag_codegen_emit_context_t *emit_context,
    const ir_data_module_t *data_module);
void gen_global_vars_in(
    ag_codegen_emit_context_t *emit_context,
    const ir_data_module_t *data_module);
#endif
