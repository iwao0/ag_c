#include "codegen_emit.h"
#include "diag/diag.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static gen_output_line_fn gen_output_cb;
static void *gen_output_user_data;

/* Peephole for trivial push/pop pairs that may be emitted by ARM64 codegen.
 * It is harmless for other text backends because it only matches exact lines. */
#define PEEPHOLE_BUF_SIZE 128
static char peephole_buf[PEEPHOLE_BUF_SIZE];
static size_t peephole_len = 0;
static int peephole_has_line = 0;

static const char STR_X0_PUSH[] = "  str x0, [sp, #-16]!\n";
static const char LDR_X0_POP[]  = "  ldr x0, [sp], #16\n";
static const char LDR_X1_POP[]  = "  ldr x1, [sp], #16\n";
static const char MOV_X1_X0[]   = "  mov x1, x0\n";

static void cg_raw_emit(const char *line, size_t len) {
  if (gen_output_cb) {
    gen_output_cb(line, len, gen_output_user_data);
  } else {
    fwrite(line, 1, len, stdout);
  }
}

static void cg_flush_peephole(void) {
  if (!peephole_has_line) return;
  cg_raw_emit(peephole_buf, peephole_len);
  peephole_has_line = 0;
  peephole_len = 0;
}

static void cg_emit_line(const char *line, size_t len) {
  if (peephole_has_line
      && peephole_len == sizeof(STR_X0_PUSH) - 1
      && memcmp(peephole_buf, STR_X0_PUSH, peephole_len) == 0) {
    if (len == sizeof(LDR_X0_POP) - 1
        && memcmp(line, LDR_X0_POP, len) == 0) {
      peephole_has_line = 0;
      peephole_len = 0;
      return;
    }
    if (len == sizeof(LDR_X1_POP) - 1
        && memcmp(line, LDR_X1_POP, len) == 0) {
      peephole_has_line = 0;
      peephole_len = 0;
      cg_raw_emit(MOV_X1_X0, sizeof(MOV_X1_X0) - 1);
      return;
    }
  }
  cg_flush_peephole();
  if (len < PEEPHOLE_BUF_SIZE) {
    memcpy(peephole_buf, line, len);
    peephole_len = len;
    peephole_has_line = 1;
  } else {
    cg_raw_emit(line, len);
  }
}

void cg_emitf(const char *fmt, ...) {
  char stack_buf[256];
  va_list ap;
  va_start(ap, fmt);
  va_list ap2;
  va_copy(ap2, ap);
  int need_i = vsnprintf(stack_buf, sizeof(stack_buf), fmt, ap2);
  va_end(ap2);
  if (need_i < 0) {
    va_end(ap);
    diag_emit_internalf(DIAG_ERR_CODEGEN_OUTPUT_FAILED, "%s",
                        diag_message_for(DIAG_ERR_CODEGEN_OUTPUT_FAILED));
  }
  size_t need = (size_t)need_i;
  char *buf = stack_buf;
  char *heap_buf = NULL;
  if (need >= sizeof(stack_buf)) {
    heap_buf = malloc(need + 1);
    if (!heap_buf) {
      va_end(ap);
      diag_emit_internalf(DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for(DIAG_ERR_INTERNAL_OOM));
    }
    vsnprintf(heap_buf, need + 1, fmt, ap);
    buf = heap_buf;
  }
  va_end(ap);
  size_t start = 0;
  for (size_t i = 0; i < need; i++) {
    if (buf[i] == '\n') {
      cg_emit_line(buf + start, i - start + 1);
      start = i + 1;
    }
  }
  if (start < need) {
    cg_emit_line(buf + start, need - start);
  }
  free(heap_buf);
}

void gen_set_output_callback(gen_output_line_fn cb, void *user_data) {
  cg_flush_peephole();
  gen_output_cb = cb;
  gen_output_user_data = user_data;
}
