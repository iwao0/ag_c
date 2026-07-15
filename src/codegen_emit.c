#include "codegen_emit.h"
#include "diag/diag.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

struct ag_codegen_emit_context_t {
  gen_output_line_fn output_cb;
  void *output_user_data;
  char format_stack_buf[256];
  int simple_formatter;
};

ag_codegen_emit_context_t *cg_context_create(void) {
  return calloc(1, sizeof(ag_codegen_emit_context_t));
}

void cg_context_destroy(ag_codegen_emit_context_t *ctx) {
  if (!ctx) return;
  free(ctx);
}

static void cg_raw_emit(
    ag_codegen_emit_context_t *ctx, const char *line, size_t len) {
  if (ctx->output_cb) {
    ctx->output_cb(line, len, ctx->output_user_data);
  } else {
    fwrite(line, 1, len, stdout);
  }
}

static void cg_emit_char(ag_codegen_emit_context_t *ctx, int ch) {
  char c = (char)ch;
  cg_raw_emit(ctx, &c, 1);
}

static void cg_emit_cstr_n(
    ag_codegen_emit_context_t *ctx, const char *s, int limit) {
  if (!s) s = "(null)";
  size_t len = 0;
  while (s[len] && (limit < 0 || (int)len < limit)) len++;
  cg_raw_emit(ctx, s, len);
}

static void cg_emit_uint_simple(
    ag_codegen_emit_context_t *ctx, unsigned long long v,
    unsigned base, int width, int zero_pad) {
  char tmp[32];
  int n = 0;
  const char *digits = "0123456789abcdef";
  if (base < 2) base = 10;
  do {
    tmp[n++] = digits[v % base];
    v /= base;
  } while (v != 0 && n < (int)sizeof(tmp));
  char pad = zero_pad ? '0' : ' ';
  while (n < width) {
    cg_emit_char(ctx, pad);
    width--;
  }
  while (n > 0) cg_emit_char(ctx, tmp[--n]);
}

static void cg_emit_int_simple(
    ag_codegen_emit_context_t *ctx,
    long long v, int width, int zero_pad) {
  if (v < 0) {
    cg_emit_char(ctx, '-');
    cg_emit_uint_simple(
        ctx, (unsigned long long)(-(v + 1)) + 1ull,
        10, width > 0 ? width - 1 : 0, zero_pad);
  } else {
    cg_emit_uint_simple(
        ctx, (unsigned long long)v, 10, width, zero_pad);
  }
}

static void cg_vemitf_in(
    ag_codegen_emit_context_t *ctx, const char *fmt, va_list args) {
  if (!ctx || !fmt) abort();
  va_list ap;
  va_copy(ap, args);
  if (ctx->simple_formatter) {
    const char *p = fmt;
    while (*p) {
      if (*p != '%') {
        cg_emit_char(ctx, *p++);
        continue;
      }
      p++;
      if (*p == '%') {
        cg_emit_char(ctx, '%');
        p++;
        continue;
      }

      int zero_pad = 0;
      int width = 0;
      int precision = -1;
      if (*p == '0') {
        zero_pad = 1;
        p++;
      }
      while (*p >= '0' && *p <= '9') {
        width = width * 10 + (*p - '0');
        p++;
      }
      if (*p == '.') {
        p++;
        if (*p == '*') {
          precision = va_arg(ap, int);
          p++;
        } else {
          precision = 0;
          while (*p >= '0' && *p <= '9') {
            precision = precision * 10 + (*p - '0');
            p++;
          }
        }
      }

      int length_l = 0;
      int length_z = 0;
      if (*p == 'z') {
        length_z = 1;
        p++;
      } else {
        while (*p == 'l') {
          length_l++;
          p++;
        }
      }

      if (*p == 's') {
        cg_emit_cstr_n(ctx, va_arg(ap, char *), precision);
        p++;
      } else if (*p == 'c') {
        cg_emit_char(ctx, va_arg(ap, int));
        p++;
      } else if (*p == 'd') {
        long long v = length_l >= 2 ? va_arg(ap, long long) : (long long)va_arg(ap, int);
        cg_emit_int_simple(ctx, v, width, zero_pad);
        p++;
      } else if (*p == 'u') {
        unsigned long long v;
        if (length_l >= 2) v = va_arg(ap, unsigned long long);
        else if (length_z) v = (unsigned long long)va_arg(ap, size_t);
        else v = (unsigned long long)va_arg(ap, unsigned int);
        cg_emit_uint_simple(ctx, v, 10, width, zero_pad);
        p++;
      } else if (*p == 'x') {
        unsigned int v = va_arg(ap, unsigned int);
        cg_emit_uint_simple(ctx, v, 16, width, zero_pad);
        p++;
      } else if (*p == 'g' || *p == 'f') {
        (void)va_arg(ap, double);
        cg_emit_char(ctx, '0');
        p++;
      } else {
        cg_emit_char(ctx, '%');
        if (*p) cg_emit_char(ctx, *p++);
      }
    }
    va_end(ap);
    return;
  }
  int need_i = vsnprintf(
      ctx->format_stack_buf, sizeof(ctx->format_stack_buf), fmt, ap);
  va_end(ap);
  if (need_i < 0) {
    diag_emit_internalf(DIAG_ERR_CODEGEN_OUTPUT_FAILED, "%s",
                        diag_message_for(DIAG_ERR_CODEGEN_OUTPUT_FAILED));
  }
  size_t need = (size_t)need_i;
  char *buf = ctx->format_stack_buf;
  char *heap_buf = NULL;
  if (need >= sizeof(ctx->format_stack_buf)) {
    heap_buf = malloc(need + 1);
    if (!heap_buf) {
      diag_emit_internalf(DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for(DIAG_ERR_INTERNAL_OOM));
    }
    va_list heap_args;
    va_copy(heap_args, args);
    vsnprintf(heap_buf, need + 1, fmt, heap_args);
    va_end(heap_args);
    buf = heap_buf;
  }
  cg_raw_emit(ctx, buf, need);
  free(heap_buf);
}

void cg_emitf_in(
    ag_codegen_emit_context_t *ctx, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  cg_vemitf_in(ctx, fmt, args);
  va_end(args);
}

void gen_set_output_callback_in(
    ag_codegen_emit_context_t *ctx,
    gen_output_line_fn cb, void *user_data) {
  if (!ctx) abort();
  ctx->output_cb = cb;
  ctx->output_user_data = user_data;
}

void gen_set_simple_formatter_in(
    ag_codegen_emit_context_t *ctx, int enable) {
  if (!ctx) abort();
  ctx->simple_formatter = enable;
}
