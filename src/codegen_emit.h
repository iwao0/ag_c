#ifndef AG_CODEGEN_EMIT_H
#define AG_CODEGEN_EMIT_H

#include "codegen_backend.h"

typedef struct ag_codegen_emit_context_t ag_codegen_emit_context_t;
typedef struct ag_diagnostic_context_t ag_diagnostic_context_t;

ag_codegen_emit_context_t *cg_context_create(
    ag_diagnostic_context_t *diagnostic_context);
void cg_context_destroy(ag_codegen_emit_context_t *ctx);
ag_diagnostic_context_t *cg_context_diagnostics(
    const ag_codegen_emit_context_t *ctx);

void cg_emitf_in(
    ag_codegen_emit_context_t *ctx, const char *fmt, ...);
void gen_set_output_callback_in(
    ag_codegen_emit_context_t *ctx,
    gen_output_line_fn cb, void *user_data);
void gen_set_simple_formatter_in(
    ag_codegen_emit_context_t *ctx, int enable);

#endif
