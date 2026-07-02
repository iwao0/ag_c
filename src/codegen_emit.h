#ifndef AG_CODEGEN_EMIT_H
#define AG_CODEGEN_EMIT_H

#include "codegen_backend.h"

void cg_emitf(const char *fmt, ...);
void gen_set_output_callback(gen_output_line_fn cb, void *user_data);
void gen_set_simple_formatter(int enable);

#endif
