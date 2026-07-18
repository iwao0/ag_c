/*
 * ARM64 Apple ABI: IR → ASM 出力 (Phase 2 最小版)。
 */

#ifndef AG_ARM64_APPLE_IR_H
#define AG_ARM64_APPLE_IR_H

#include "../../ir/ir.h"

typedef struct ir_abi_module_t ir_abi_module_t;

typedef struct ag_codegen_emit_context_t ag_codegen_emit_context_t;

void gen_ir_module_in(
    ag_codegen_emit_context_t *emit_context, ir_module_t *m,
    const ir_abi_module_t *abi);

#endif
