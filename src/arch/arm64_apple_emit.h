/*
 * ARM64 Apple backend specific emit helpers.
 */

#ifndef AG_ARM64_APPLE_EMIT_H
#define AG_ARM64_APPLE_EMIT_H

#include "../codegen_emit.h"

/* 16bit に収まらない大きい即値も movz+movk シーケンスで安全にロードする。 */
void cg_emit_mov_imm(const char *reg, long long val);

#endif
