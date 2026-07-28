/*
 * ARM64 Apple backend specific emit helpers.
 */

#ifndef AG_ARM64_APPLE_EMIT_H
#define AG_ARM64_APPLE_EMIT_H

#include "../../codegen_emit.h"

/* 16bit に収まらない大きい即値も movz+movk シーケンスで安全にロードする。 */
void cg_emit_mov_imm_in(
    ag_codegen_emit_context_t *emit_context,
    const char *reg, long long val);

/*
 * Emit one Mach-O assembler symbol.  Symbols containing UTF-8 or other bytes
 * outside the unquoted assembler identifier set are emitted in quotes.
 */
void cg_emit_asm_symbol_in(
    ag_codegen_emit_context_t *emit_context,
    const char *prefix, const char *name, int name_len,
    const char *suffix);

#endif
