static int ag_rt_udec_len(unsigned long v) {
  int n = 1;
  while (v / 10) {
    v = v / 10;
    n++;
  }
  return n;
}

static void ag_rt_write_udec(char *buf, size_t size, int bounded, size_t *pos,
                             unsigned long v, int width, int zero_pad) {
  char tmp[32];
  int n = 0;
  do {
    tmp[n++] = (char)('0' + (v % 10));
    v = v / 10;
  } while (v != 0);
  while (!zero_pad && n < width) {
    ag_rt_putc(buf, size, bounded, pos, ' ');
    width--;
  }
  while (zero_pad && n < width) {
    ag_rt_putc(buf, size, bounded, pos, '0');
    width--;
  }
  while (n > 0) ag_rt_putc(buf, size, bounded, pos, tmp[--n]);
}

static void ag_rt_write_uint_base(char *buf, size_t size, int bounded, size_t *pos,
                                  unsigned long v, unsigned int base, int upper,
                                  int width, int zero_pad) {
  char tmp[64];
  int n = 0;
  const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
  if (base < 2 || base > 16) base = 10;
  do {
    tmp[n++] = digits[v % base];
    v = v / base;
  } while (v != 0);
  while (!zero_pad && n < width) {
    ag_rt_putc(buf, size, bounded, pos, ' ');
    width--;
  }
  while (zero_pad && n < width) {
    ag_rt_putc(buf, size, bounded, pos, '0');
    width--;
  }
  while (n > 0) ag_rt_putc(buf, size, bounded, pos, tmp[--n]);
}

static void ag_rt_write_spaces(char *buf, size_t size, int bounded, size_t *pos, int count) {
  while (count > 0) {
    ag_rt_putc(buf, size, bounded, pos, ' ');
    count--;
  }
}

static void ag_rt_write_bytes(char *buf, size_t size, int bounded, size_t *pos,
                              const char *src, int len) {
  for (int i = 0; i < len; i++) ag_rt_putc(buf, size, bounded, pos, src[i]);
}

static void ag_rt_write_pointer(char *buf, size_t size, int bounded, size_t *pos,
                                unsigned long v, int width, int left_align) {
  char tmp[64];
  size_t tmp_pos = 0;
  ag_rt_putc(tmp, sizeof(tmp), 1, &tmp_pos, '0');
  ag_rt_putc(tmp, sizeof(tmp), 1, &tmp_pos, 'x');
  ag_rt_write_uint_base(tmp, sizeof(tmp), 1, &tmp_pos, v, 16, 0, 0, 0);
  ag_rt_finish(tmp, sizeof(tmp), 1, tmp_pos);
  if (!left_align) ag_rt_write_spaces(buf, size, bounded, pos, width - (int)tmp_pos);
  ag_rt_write_bytes(buf, size, bounded, pos, tmp, (int)tmp_pos);
  if (left_align) ag_rt_write_spaces(buf, size, bounded, pos, width - (int)tmp_pos);
}

static void ag_rt_write_idec(char *buf, size_t size, int bounded, size_t *pos,
                             int v, int width, int zero_pad) {
  unsigned long u;
  int negative = v < 0;
  if (negative) {
    u = (unsigned long)(-(long)v);
  } else {
    u = (unsigned long)v;
  }

  int digits = ag_rt_udec_len(u);
  int total = digits + negative;
  while (!zero_pad && total < width) {
    ag_rt_putc(buf, size, bounded, pos, ' ');
    total++;
  }
  if (negative) ag_rt_putc(buf, size, bounded, pos, '-');
  while (zero_pad && total < width) {
    ag_rt_putc(buf, size, bounded, pos, '0');
    total++;
  }
  ag_rt_write_udec(buf, size, bounded, pos, u, 0, 0);
}

static void ag_rt_write_fixed(char *buf, size_t size, int bounded, size_t *pos,
                              double v, int precision) {
  if (precision < 0) precision = 6;
  if (precision > 9) precision = 9;
  if (v < 0.0) {
    ag_rt_putc(buf, size, bounded, pos, '-');
    v = -v;
  }

  unsigned long scale = 1;
  for (int i = 0; i < precision; i++) scale *= 10;
  double rounded = v + 0.5 / (double)scale;
  unsigned long whole = (unsigned long)rounded;
  double frac_ld = (rounded - (double)whole) * (double)scale;
  unsigned long frac = (unsigned long)frac_ld;

  ag_rt_write_udec(buf, size, bounded, pos, whole, 0, 0);
  if (precision == 0) return;
  ag_rt_putc(buf, size, bounded, pos, '.');
  unsigned long div = scale / 10;
  while (div > 0) {
    ag_rt_putc(buf, size, bounded, pos, '0' + (int)(frac / div));
    frac %= div;
    div /= 10;
  }
}

static void ag_rt_write_str_n(char *buf, size_t size, int bounded, size_t *pos,
                              const char *s, int limit) {
  if (limit < 0) {
    ag_rt_write_str(buf, size, bounded, pos, s);
    return;
  }
  if (!s) s = "(null)";
  while (*s && limit > 0) {
    ag_rt_putc(buf, size, bounded, pos, *s);
    s++;
    limit--;
  }
}

static int ag_rt_strn_len(const char *s, int limit) {
  int n = 0;
  if (!s) s = "(null)";
  while (s[n] && (limit < 0 || n < limit)) n++;
  return n;
}

static int ag_rt_vformat(char *buf, size_t size, int bounded, const char *fmt, va_list ap) {
  size_t pos = 0;
  while (*fmt) {
    if (*fmt != '%') {
      ag_rt_putc(buf, size, bounded, &pos, *fmt++);
      continue;
    }
    fmt++;
    if (*fmt == '%') {
      ag_rt_putc(buf, size, bounded, &pos, '%');
      fmt++;
      continue;
    }

    int left_align = 0;
    int zero_pad = 0;
    int width = 0;
    int precision = -1;
    for (;;) {
      if (*fmt == '-') {
        left_align = 1;
        fmt++;
      } else if (*fmt == '0') {
        zero_pad = 1;
        fmt++;
      } else {
        break;
      }
    }
    while ((int)*fmt >= '0' && (int)*fmt <= '9') {
      width = width * 10 + (*fmt - '0');
      fmt++;
    }
    if (*fmt == '*') {
      width = va_arg(ap, int);
      if (width < 0) {
        left_align = 1;
        width = -width;
      }
      fmt++;
    }
    if (left_align) zero_pad = 0;
    if (*fmt == '.') {
      fmt++;
      if (*fmt == '*') {
        precision = va_arg(ap, int);
        fmt++;
      } else {
        precision = 0;
        while ((int)*fmt >= '0' && (int)*fmt <= '9') {
          precision = precision * 10 + (*fmt - '0');
          fmt++;
        }
      }
    }

    int length_z = 0;
    int length_l = 0;
    int length_L = 0;
    if (*fmt == 'z') {
      length_z = 1;
      fmt++;
    } else if (*fmt == 'l') {
      length_l = 1;
      fmt++;
    } else if (*fmt == 'L') {
      length_L = 1;
      fmt++;
    }

    if (*fmt == 'd') {
      int v = va_arg(ap, int);
      if (left_align) {
        char tmp[64];
        size_t tmp_pos = 0;
        ag_rt_write_idec(tmp, sizeof(tmp), 1, &tmp_pos, v, 0, 0);
        ag_rt_finish(tmp, sizeof(tmp), 1, tmp_pos);
        ag_rt_write_bytes(buf, size, bounded, &pos, tmp, (int)tmp_pos);
        ag_rt_write_spaces(buf, size, bounded, &pos, width - (int)tmp_pos);
      } else {
        ag_rt_write_idec(buf, size, bounded, &pos, v, width, zero_pad);
      }
      fmt++;
    } else if (*fmt == 'u') {
      unsigned long v = length_z ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
      if (left_align) {
        char tmp[64];
        size_t tmp_pos = 0;
        ag_rt_write_udec(tmp, sizeof(tmp), 1, &tmp_pos, v, 0, 0);
        ag_rt_finish(tmp, sizeof(tmp), 1, tmp_pos);
        ag_rt_write_bytes(buf, size, bounded, &pos, tmp, (int)tmp_pos);
        ag_rt_write_spaces(buf, size, bounded, &pos, width - (int)tmp_pos);
      } else {
        ag_rt_write_udec(buf, size, bounded, &pos, v, width, zero_pad);
      }
      fmt++;
    } else if (*fmt == 'x' || *fmt == 'X' || *fmt == 'o') {
      int upper = *fmt == 'X';
      unsigned int base = *fmt == 'o' ? 8u : 16u;
      unsigned long v = length_z ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
      if (left_align) {
        char tmp[64];
        size_t tmp_pos = 0;
        ag_rt_write_uint_base(tmp, sizeof(tmp), 1, &tmp_pos, v, base, upper, 0, 0);
        ag_rt_finish(tmp, sizeof(tmp), 1, tmp_pos);
        ag_rt_write_bytes(buf, size, bounded, &pos, tmp, (int)tmp_pos);
        ag_rt_write_spaces(buf, size, bounded, &pos, width - (int)tmp_pos);
      } else {
        ag_rt_write_uint_base(buf, size, bounded, &pos, v, base, upper, width, zero_pad);
      }
      fmt++;
    } else if (*fmt == 'p') {
      void *p = va_arg(ap, void *);
      ag_rt_write_pointer(buf, size, bounded, &pos, (unsigned long)(long)p, width, left_align);
      fmt++;
    } else if (*fmt == 's') {
      char *s = va_arg(ap, char *);
      int len = ag_rt_strn_len(s, precision);
      if (!left_align) ag_rt_write_spaces(buf, size, bounded, &pos, width - len);
      ag_rt_write_str_n(buf, size, bounded, &pos, s, precision);
      if (left_align) ag_rt_write_spaces(buf, size, bounded, &pos, width - len);
      fmt++;
    } else if (*fmt == 'c') {
      int ch = va_arg(ap, int);
      if (!left_align) ag_rt_write_spaces(buf, size, bounded, &pos, width - 1);
      ag_rt_putc(buf, size, bounded, &pos, ch);
      if (left_align) ag_rt_write_spaces(buf, size, bounded, &pos, width - 1);
      fmt++;
    } else if (*fmt == 'f' || *fmt == 'F') {
      (void)length_l;
      double v;
      if (length_L) {
        long double lv = va_arg(ap, long double);
        v = (double)lv;
      } else {
        v = va_arg(ap, double);
      }
      if (left_align) {
        char tmp[128];
        size_t tmp_pos = 0;
        ag_rt_write_fixed(tmp, sizeof(tmp), 1, &tmp_pos, v, precision);
        ag_rt_finish(tmp, sizeof(tmp), 1, tmp_pos);
        ag_rt_write_bytes(buf, size, bounded, &pos, tmp, (int)tmp_pos);
        ag_rt_write_spaces(buf, size, bounded, &pos, width - (int)tmp_pos);
      } else {
        ag_rt_write_fixed(buf, size, bounded, &pos, v, precision);
      }
      fmt++;
    } else {
      ag_rt_putc(buf, size, bounded, &pos, '%');
      if (*fmt) ag_rt_putc(buf, size, bounded, &pos, *fmt++);
    }
  }
  ag_rt_finish(buf, size, bounded, pos);
  return (int)pos;
}

static void ag_rt_wputc(int *buf, size_t size, size_t *pos, int ch) {
  if ((long)(*pos + 1) < (long)size) buf[(long)*pos] = ch;
  *pos = *pos + 1;
}

static void ag_rt_wfinish(int *buf, size_t size, size_t pos) {
  if (size != 0) buf[(long)((long)pos < (long)size ? pos : size - 1)] = 0;
}

static void ag_rt_wwrite_str(int *buf, size_t size, size_t *pos, const char *s) {
  if (!s) s = "(null)";
  while (*s) {
    ag_rt_wputc(buf, size, pos, (unsigned char)*s);
    s++;
  }
}

static void ag_rt_wwrite_wstr(int *buf, size_t size, size_t *pos, int *s) {
  if (!s) {
    ag_rt_wwrite_str(buf, size, pos, "(null)");
    return;
  }
  while (*s) {
    ag_rt_wputc(buf, size, pos, *s);
    s++;
  }
}

static void ag_rt_wwrite_udec(int *buf, size_t size, size_t *pos, unsigned long v) {
  unsigned long div = 1000000000UL;
  int started = 0;
  while (div > 0) {
    int digit = (int)(v / div);
    if (digit || started || div == 1) {
      ag_rt_wputc(buf, size, pos, '0' + digit);
      started = 1;
    }
    v = v % div;
    div = div / 10;
  }
}

static void ag_rt_wwrite_idec(int *buf, size_t size, size_t *pos, int v) {
  unsigned int u;
  if (v < 0) {
    ag_rt_wputc(buf, size, pos, '-');
    u = (unsigned int)(-(v + 1)) + 1u;
  } else {
    u = (unsigned int)v;
  }
  ag_rt_wwrite_udec(buf, size, pos, u);
}

static int ag_rt_vwformat(int *buf, size_t size, int *fmt, va_list ap) {
  size_t pos = 0;
  while (*fmt) {
    if (*fmt != '%') {
      ag_rt_wputc(buf, size, &pos, *fmt++);
      continue;
    }
    fmt++;
    if (*fmt == '%') {
      ag_rt_wputc(buf, size, &pos, '%');
      fmt++;
      continue;
    }

    int length_l = 0;
    if (*fmt == 'l') {
      length_l = 1;
      fmt++;
    }

    if (*fmt == 'd') {
      ag_rt_wwrite_idec(buf, size, &pos, va_arg(ap, int));
      fmt++;
    } else if (*fmt == 'u') {
      ag_rt_wwrite_udec(buf, size, &pos, (unsigned long)va_arg(ap, unsigned int));
      fmt++;
    } else if (*fmt == 's') {
      if (length_l) {
        ag_rt_wwrite_wstr(buf, size, &pos, va_arg(ap, int *));
      } else {
        ag_rt_wwrite_str(buf, size, &pos, va_arg(ap, char *));
      }
      fmt++;
    } else if (*fmt == 'c') {
      ag_rt_wputc(buf, size, &pos, va_arg(ap, int));
      fmt++;
    } else {
      ag_rt_wputc(buf, size, &pos, '%');
      if (length_l) ag_rt_wputc(buf, size, &pos, 'l');
      if (*fmt) ag_rt_wputc(buf, size, &pos, *fmt++);
    }
  }
  ag_rt_wfinish(buf, size, pos);
  return (int)pos;
}

int __agc_runtime_snprintf(long buf_addr, size_t size, long fmt_addr, ...) {
  char *buf = (char *)(long)buf_addr;
  char *fmt = (char *)(long)fmt_addr;
  va_list ap;
  va_start(ap, fmt_addr);
  int n = ag_rt_vformat(buf, size, 1, fmt, ap);
  va_end(ap);
  return n;
}

int __agc_runtime_vsnprintf(long buf_addr, size_t size, long fmt_addr, long ap_addr) {
  char *buf = (char *)(long)buf_addr;
  char *fmt = (char *)(long)fmt_addr;
  va_list ap = (va_list)(long)ap_addr;
  return ag_rt_vformat(buf, size, 1, fmt, ap);
}

int __agc_runtime_sprintf(long buf_addr, long fmt_addr, ...) {
  char *buf = (char *)(long)buf_addr;
  char *fmt = (char *)(long)fmt_addr;
  va_list ap;
  va_start(ap, fmt_addr);
  int n = ag_rt_vformat(buf, 0, 0, fmt, ap);
  va_end(ap);
  return n;
}

int __agc_runtime_printf(long fmt_addr, ...) {
  char *fmt = (char *)(long)fmt_addr;
  va_list ap;
  va_start(ap, fmt_addr);
  char buf[1024];
  int n = ag_rt_vformat(buf, sizeof(buf), 1, fmt, ap);
  va_end(ap);
  ag_rt_stdout_write_mem(buf, n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1);
  return n;
}

static int ag_rt_write_formatted_stream(long stream_addr, char *buf, int n) {
  int len = n < 1024 ? n : 1023;
  if (len < 0) len = 0;
  if (ag_rt_is_stderr_stream(stream_addr)) {
    ag_rt_stderr_write_mem(buf, len);
  } else if (ag_rt_is_stdout_stream(stream_addr)) {
    ag_rt_stdout_write_mem(buf, len);
  } else {
    struct ag_rt_file *f = ag_rt_input_stream(stream_addr);
    if (!f) return -1;
    if (ag_rt_file_write_mem(f, buf, len) != len) return -1;
  }
  return n;
}

int __agc_runtime_fprintf(long stream_addr, long fmt_addr, ...) {
  char *fmt = (char *)(long)fmt_addr;
  va_list ap;
  va_start(ap, fmt_addr);
  char buf[1024];
  int n = ag_rt_vformat(buf, sizeof(buf), 1, fmt, ap);
  va_end(ap);
  return ag_rt_write_formatted_stream(stream_addr, buf, n);
}

int __agc_runtime_vfprintf(long stream_addr, long fmt_addr, long ap_addr) {
  char *fmt = (char *)(long)fmt_addr;
  va_list ap = (va_list)(long)ap_addr;
  char buf[1024];
  int n = ag_rt_vformat(buf, sizeof(buf), 1, fmt, ap);
  return ag_rt_write_formatted_stream(stream_addr, buf, n);
}

int __agc_runtime_swprintf(long buf_addr, size_t size, long fmt_addr, ...) {
  int *buf = (int *)(long)buf_addr;
  int *fmt = (int *)(long)fmt_addr;
  va_list ap;
  va_start(ap, fmt_addr);
  int n = ag_rt_vwformat(buf, size, fmt, ap);
  va_end(ap);
  return n;
}

int __agc_runtime_swscanf(long s_addr, long fmt_addr, ...) {
  (void)s_addr;
  (void)fmt_addr;
  return 0;
}

void __agc_runtime___assert_rtn(long func_addr, long file_addr, int line, long expr_addr) {
  (void)func_addr;
  (void)file_addr;
  (void)line;
  (void)expr_addr;
  for (;;) {
  }
}
