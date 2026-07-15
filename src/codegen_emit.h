#ifndef AG_CODEGEN_EMIT_H
#define AG_CODEGEN_EMIT_H

#include "codegen_backend.h"

typedef struct ag_codegen_emit_context_t ag_codegen_emit_context_t;

ag_codegen_emit_context_t *cg_context_create(void);
void cg_context_destroy(ag_codegen_emit_context_t *ctx);
ag_codegen_emit_context_t *cg_context_activate(
    ag_codegen_emit_context_t *ctx);
ag_codegen_emit_context_t *cg_context_active(void);

void cg_emitf(const char *fmt, ...);
void cg_emitf_in(
    ag_codegen_emit_context_t *ctx, const char *fmt, ...);
void gen_set_output_callback(gen_output_line_fn cb, void *user_data);
void gen_set_output_callback_in(
    ag_codegen_emit_context_t *ctx,
    gen_output_line_fn cb, void *user_data);
void gen_set_simple_formatter(int enable);
void gen_set_simple_formatter_in(
    ag_codegen_emit_context_t *ctx, int enable);

#endif
