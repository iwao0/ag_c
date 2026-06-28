#include "codegen_emit.h"
#include "diag/diag.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static gen_output_line_fn gen_output_cb;
static void *gen_output_user_data;

static void cg_raw_emit(const char *line, size_t len) {
  if (gen_output_cb) {
    gen_output_cb(line, len, gen_output_user_data);
  } else {
    fwrite(line, 1, len, stdout);
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
  cg_raw_emit(buf, need);
  free(heap_buf);
}

void gen_set_output_callback(gen_output_line_fn cb, void *user_data) {
  gen_output_cb = cb;
  gen_output_user_data = user_data;
}
