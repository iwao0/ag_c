/*
 * ARM64 Apple ABI: IR → ASM 出力 (Phase 2 最小版)。
 */

#ifndef AG_ARM64_APPLE_IR_H
#define AG_ARM64_APPLE_IR_H

#include "../ir/ir.h"

void gen_ir_module(ir_module_t *m);

#endif
