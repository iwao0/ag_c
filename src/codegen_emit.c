#include "codegen_emit.h"
#include "diag/diag.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static gen_output_line_fn gen_output_cb;
static void *gen_output_user_data;
static char cg_format_stack_buf[256];
static int gen_simple_formatter;

static void cg_raw_emit(const char *line, size_t len) {
  if (gen_output_cb) {
    gen_output_cb(line, len, gen_output_user_data);
  } else {
    fwrite(line, 1, len, stdout);
  }
}

static void cg_emit_char(int ch) {
  char c = (char)ch;
  cg_raw_emit(&c, 1);
}

static void cg_emit_cstr_n(const char *s, int limit) {
  if (!s) s = "(null)";
  size_t len = 0;
  while (s[len] && (limit < 0 || (int)len < limit)) len++;
  cg_raw_emit(s, len);
}

static void cg_emit_uint_simple(unsigned long long v, unsigned base, int width, int zero_pad) {
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
    cg_emit_char(pad);
    width--;
  }
  while (n > 0) cg_emit_char(tmp[--n]);
}

static void cg_emit_int_simple(long long v, int width, int zero_pad) {
  if (v < 0) {
    cg_emit_char('-');
    cg_emit_uint_simple((unsigned long long)(-(v + 1)) + 1ull, 10, width > 0 ? width - 1 : 0, zero_pad);
  } else {
    cg_emit_uint_simple((unsigned long long)v, 10, width, zero_pad);
  }
}

void cg_emitf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  if (gen_simple_formatter) {
    const char *p = fmt;
    while (*p) {
      if (*p != '%') {
        cg_emit_char(*p++);
        continue;
      }
      p++;
      if (*p == '%') {
        cg_emit_char('%');
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
        cg_emit_cstr_n(va_arg(ap, char *), precision);
        p++;
      } else if (*p == 'c') {
        cg_emit_char(va_arg(ap, int));
        p++;
      } else if (*p == 'd') {
        long long v = length_l >= 2 ? va_arg(ap, long long) : (long long)va_arg(ap, int);
        cg_emit_int_simple(v, width, zero_pad);
        p++;
      } else if (*p == 'u') {
        unsigned long long v;
        if (length_l >= 2) v = va_arg(ap, unsigned long long);
        else if (length_z) v = (unsigned long long)va_arg(ap, size_t);
        else v = (unsigned long long)va_arg(ap, unsigned int);
        cg_emit_uint_simple(v, 10, width, zero_pad);
        p++;
      } else if (*p == 'x') {
        unsigned int v = va_arg(ap, unsigned int);
        cg_emit_uint_simple(v, 16, width, zero_pad);
        p++;
      } else if (*p == 'g' || *p == 'f') {
        (void)va_arg(ap, double);
        cg_emit_char('0');
        p++;
      } else {
        cg_emit_char('%');
        if (*p) cg_emit_char(*p++);
      }
    }
    va_end(ap);
    return;
  }
  va_list ap2;
  va_copy(ap2, ap);
  int need_i = vsnprintf(cg_format_stack_buf, sizeof(cg_format_stack_buf), fmt, ap2);
  va_end(ap2);
  if (need_i < 0) {
    va_end(ap);
    diag_emit_internalf(DIAG_ERR_CODEGEN_OUTPUT_FAILED, "%s",
                        diag_message_for(DIAG_ERR_CODEGEN_OUTPUT_FAILED));
  }
  size_t need = (size_t)need_i;
  char *buf = cg_format_stack_buf;
  char *heap_buf = NULL;
  if (need >= sizeof(cg_format_stack_buf)) {
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

void gen_set_simple_formatter(int enable) {
  gen_simple_formatter = enable;
}
