/*
 * arm64_apple.c が公開する低レベル emit ヘルパ。
 * arm64_apple_ir.c (IR バックエンド) から共有して使う。
 */

#ifndef AG_ARM64_APPLE_EMIT_H
#define AG_ARM64_APPLE_EMIT_H

void cg_emitf(const char *fmt, ...);

#endif
