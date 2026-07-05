#define AG_RT_DECIMAL_FORMAT_MAX_PRECISION 18

static int ag_rt_udec_len(unsigned long v) {
  int n = 1;
  while (v / 10) {
    v = v / 10;
    n++;
  }
  return n;
}

static void ag_rt_write_udec_prec(char *buf, size_t size, int bounded, size_t *pos,
                                  unsigned long v, int width, int zero_pad, int precision) {
  char tmp[32];
  int n = 0;
  int zero_count;
  if (!(precision == 0 && v == 0)) {
    do {
      tmp[n++] = (char)('0' + (v % 10));
      v = v / 10;
    } while (v != 0);
  }
  zero_count = precision > n ? precision - n : 0;
  while (!zero_pad && n + zero_count < width) {
    ag_rt_putc(buf, size, bounded, pos, ' ');
    width--;
  }
  while (zero_pad && n + zero_count < width) {
    ag_rt_putc(buf, size, bounded, pos, '0');
    width--;
  }
  while (zero_count > 0) {
    ag_rt_putc(buf, size, bounded, pos, '0');
    zero_count--;
  }
  while (n > 0) ag_rt_putc(buf, size, bounded, pos, tmp[--n]);
}

static void ag_rt_write_udec(char *buf, size_t size, int bounded, size_t *pos,
                             unsigned long v, int width, int zero_pad) {
  ag_rt_write_udec_prec(buf, size, bounded, pos, v, width, zero_pad, -1);
}

static void ag_rt_write_uint_base(char *buf, size_t size, int bounded, size_t *pos,
                                  unsigned long v, unsigned int base, int upper,
                                  int width, int zero_pad, int precision, int alternate) {
  char tmp[64];
  int n = 0;
  int zero_count;
  int prefix_len = 0;
  unsigned long orig = v;
  const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
  if (base < 2 || base > 16) base = 10;
  if (!(precision == 0 && v == 0)) {
    do {
      tmp[n++] = digits[v % base];
      v = v / base;
    } while (v != 0);
  }
  zero_count = precision > n ? precision - n : 0;
  if (alternate && base == 16 && orig != 0) {
    prefix_len = 2;
  } else if (alternate && base == 8 &&
             ((orig == 0 && precision == 0) || (orig != 0 && precision <= n))) {
    prefix_len = 1;
  }
  while (!zero_pad && prefix_len + n + zero_count < width) {
    ag_rt_putc(buf, size, bounded, pos, ' ');
    width--;
  }
  if (prefix_len == 1) {
    ag_rt_putc(buf, size, bounded, pos, '0');
  } else if (prefix_len == 2) {
    ag_rt_putc(buf, size, bounded, pos, '0');
    ag_rt_putc(buf, size, bounded, pos, upper ? 'X' : 'x');
  }
  while (zero_pad && prefix_len + n + zero_count < width) {
    ag_rt_putc(buf, size, bounded, pos, '0');
    width--;
  }
  while (zero_count > 0) {
    ag_rt_putc(buf, size, bounded, pos, '0');
    zero_count--;
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
  ag_rt_write_uint_base(tmp, sizeof(tmp), 1, &tmp_pos, v, 16, 0, 0, 0, -1, 0);
  ag_rt_finish(tmp, sizeof(tmp), 1, tmp_pos);
  if (!left_align) ag_rt_write_spaces(buf, size, bounded, pos, width - (int)tmp_pos);
  ag_rt_write_bytes(buf, size, bounded, pos, tmp, (int)tmp_pos);
  if (left_align) ag_rt_write_spaces(buf, size, bounded, pos, width - (int)tmp_pos);
}

static void ag_rt_write_idec(char *buf, size_t size, int bounded, size_t *pos,
                             long v, int width, int zero_pad, int precision, int sign_ch) {
  unsigned long u;
  int negative = v < 0;
  int sign_len;
  int digits;
  int zero_count;
  int total;
  if (negative) {
    u = (unsigned long)(-(v + 1)) + 1u;
  } else {
    u = (unsigned long)v;
  }

  sign_len = negative || sign_ch ? 1 : 0;
  digits = (precision == 0 && u == 0) ? 0 : ag_rt_udec_len(u);
  zero_count = precision > digits ? precision - digits : 0;
  total = digits + zero_count + sign_len;
  while (!zero_pad && total < width) {
    ag_rt_putc(buf, size, bounded, pos, ' ');
    total++;
  }
  if (negative) {
    ag_rt_putc(buf, size, bounded, pos, '-');
  } else if (sign_ch) {
    ag_rt_putc(buf, size, bounded, pos, sign_ch);
  }
  while (zero_pad && total < width) {
    ag_rt_putc(buf, size, bounded, pos, '0');
    total++;
  }
  ag_rt_write_udec_prec(buf, size, bounded, pos, u, 0, 0, precision);
}

static int ag_rt_double_is_nan(double v) {
  return !(v <= 0.0 || v > 0.0);
}

static int ag_rt_double_is_inf(double v) {
  double zero = 0.0;
  double inf = 1.0 / zero;
  return v == inf || v == -inf;
}

static int ag_rt_double_is_negative(double v) {
  double zero = 0.0;
  if (v < 0.0) return 1;
  return v == 0.0 && (1.0 / v) == -(1.0 / zero);
}

static int ag_rt_decimal_format_precision(int precision) {
  if (precision < 0) return 6;
  if (precision > AG_RT_DECIMAL_FORMAT_MAX_PRECISION) return AG_RT_DECIMAL_FORMAT_MAX_PRECISION;
  return precision;
}

static int ag_rt_general_format_precision(int precision) {
  precision = ag_rt_decimal_format_precision(precision);
  return precision == 0 ? 1 : precision;
}

static void ag_rt_write_fixed(char *buf, size_t size, int bounded, size_t *pos,
                              double v, int precision, int upper, int alternate, int sign_ch) {
  if (ag_rt_double_is_nan(v)) {
    if (sign_ch) ag_rt_putc(buf, size, bounded, pos, sign_ch);
    ag_rt_putc(buf, size, bounded, pos, upper ? 'N' : 'n');
    ag_rt_putc(buf, size, bounded, pos, upper ? 'A' : 'a');
    ag_rt_putc(buf, size, bounded, pos, upper ? 'N' : 'n');
    return;
  }
  if (ag_rt_double_is_inf(v)) {
    if (v < 0.0) {
      ag_rt_putc(buf, size, bounded, pos, '-');
    } else if (sign_ch) {
      ag_rt_putc(buf, size, bounded, pos, sign_ch);
    }
    ag_rt_putc(buf, size, bounded, pos, upper ? 'I' : 'i');
    ag_rt_putc(buf, size, bounded, pos, upper ? 'N' : 'n');
    ag_rt_putc(buf, size, bounded, pos, upper ? 'F' : 'f');
    return;
  }
  precision = ag_rt_decimal_format_precision(precision);
  if (ag_rt_double_is_negative(v)) {
    ag_rt_putc(buf, size, bounded, pos, '-');
    v = -v;
  } else if (sign_ch) {
    ag_rt_putc(buf, size, bounded, pos, sign_ch);
  }

  unsigned long scale = 1;
  for (int i = 0; i < precision; i++) scale *= 10;
  double rounded = v + 0.5 / (double)scale;
  unsigned long whole = (unsigned long)rounded;
  double frac_ld = (rounded - (double)whole) * (double)scale;
  unsigned long frac = (unsigned long)frac_ld;

  ag_rt_write_udec(buf, size, bounded, pos, whole, 0, 0);
  if (precision == 0) {
    if (alternate) ag_rt_putc(buf, size, bounded, pos, '.');
    return;
  }
  ag_rt_putc(buf, size, bounded, pos, '.');
  unsigned long div = scale / 10;
  while (div > 0) {
    ag_rt_putc(buf, size, bounded, pos, '0' + (int)(frac / div));
    frac %= div;
    div /= 10;
  }
}

static void ag_rt_write_float_text_padded(char *buf, size_t size, int bounded, size_t *pos,
                                          const char *tmp, int len, int width,
                                          int zero_pad, int allow_zero_pad) {
  int pad;
  pad = width - len;
  if (pad <= 0) {
    ag_rt_write_bytes(buf, size, bounded, pos, tmp, len);
    return;
  }
  if (zero_pad && allow_zero_pad) {
    int start = 0;
    if (tmp[0] == '-' || tmp[0] == '+' || tmp[0] == ' ') {
      ag_rt_putc(buf, size, bounded, pos, tmp[0]);
      start = 1;
    }
    while (pad > 0) {
      ag_rt_putc(buf, size, bounded, pos, '0');
      pad--;
    }
    ag_rt_write_bytes(buf, size, bounded, pos, tmp + start, len - start);
  } else {
    ag_rt_write_spaces(buf, size, bounded, pos, pad);
    ag_rt_write_bytes(buf, size, bounded, pos, tmp, len);
  }
}

static void ag_rt_write_fixed_padded(char *buf, size_t size, int bounded, size_t *pos,
                                     double v, int width, int zero_pad, int precision,
                                     int upper, int alternate, int sign_ch) {
  char tmp[128];
  size_t tmp_pos = 0;
  ag_rt_write_fixed(tmp, sizeof(tmp), 1, &tmp_pos, v, precision, upper, alternate, sign_ch);
  ag_rt_finish(tmp, sizeof(tmp), 1, tmp_pos);
  ag_rt_write_float_text_padded(buf, size, bounded, pos, tmp, (int)tmp_pos, width,
                                zero_pad, !ag_rt_double_is_nan(v) && !ag_rt_double_is_inf(v));
}

static void ag_rt_write_exp_suffix(char *buf, size_t size, int bounded, size_t *pos,
                                   int exp, int upper) {
  int negative = exp < 0;
  unsigned long mag;
  ag_rt_putc(buf, size, bounded, pos, upper ? 'E' : 'e');
  ag_rt_putc(buf, size, bounded, pos, negative ? '-' : '+');
  mag = negative ? (unsigned long)(-exp) : (unsigned long)exp;
  if (mag < 10) ag_rt_putc(buf, size, bounded, pos, '0');
  ag_rt_write_udec(buf, size, bounded, pos, mag, 0, 0);
}

static void ag_rt_write_scientific(char *buf, size_t size, int bounded, size_t *pos,
                                   double v, int precision, int upper, int alternate, int sign_ch) {
  int exp = 0;
  if (ag_rt_double_is_nan(v) || ag_rt_double_is_inf(v)) {
    ag_rt_write_fixed(buf, size, bounded, pos, v, precision, upper, alternate, sign_ch);
    return;
  }
  precision = ag_rt_decimal_format_precision(precision);
  if (ag_rt_double_is_negative(v)) {
    ag_rt_putc(buf, size, bounded, pos, '-');
    v = -v;
  } else if (sign_ch) {
    ag_rt_putc(buf, size, bounded, pos, sign_ch);
  }
  if (v != 0.0) {
    while (v >= 10.0) {
      v = v / 10.0;
      exp++;
    }
    while (v < 1.0) {
      v = v * 10.0;
      exp--;
    }
  }

  unsigned long scale = 1;
  for (int i = 0; i < precision; i++) scale *= 10;
  double rounded = v + 0.5 / (double)scale;
  if (rounded >= 10.0) {
    rounded = rounded / 10.0;
    exp++;
  }
  unsigned long whole = (unsigned long)rounded;
  double frac_ld = (rounded - (double)whole) * (double)scale;
  unsigned long frac = (unsigned long)frac_ld;

  ag_rt_putc(buf, size, bounded, pos, '0' + (int)whole);
  if (precision != 0 || alternate) {
    ag_rt_putc(buf, size, bounded, pos, '.');
  }
  if (precision != 0) {
    unsigned long div = scale / 10;
    while (div > 0) {
      ag_rt_putc(buf, size, bounded, pos, '0' + (int)(frac / div));
      frac %= div;
      div /= 10;
    }
  }
  ag_rt_write_exp_suffix(buf, size, bounded, pos, exp, upper);
}

static void ag_rt_write_scientific_padded(char *buf, size_t size, int bounded, size_t *pos,
                                          double v, int width, int zero_pad,
                                          int precision, int upper, int alternate, int sign_ch) {
  char tmp[128];
  size_t tmp_pos = 0;
  ag_rt_write_scientific(tmp, sizeof(tmp), 1, &tmp_pos, v, precision, upper, alternate, sign_ch);
  ag_rt_finish(tmp, sizeof(tmp), 1, tmp_pos);
  ag_rt_write_float_text_padded(buf, size, bounded, pos, tmp, (int)tmp_pos, width,
                                zero_pad, !ag_rt_double_is_nan(v) && !ag_rt_double_is_inf(v));
}

static int ag_rt_float_exp10_rounded(double v, int precision) {
  int exp = 0;
  unsigned long scale = 1;
  if (ag_rt_double_is_negative(v)) v = -v;
  if (v == 0.0) return 0;
  while (v >= 10.0) {
    v = v / 10.0;
    exp++;
  }
  while (v < 1.0) {
    v = v * 10.0;
    exp--;
  }
  for (int i = 1; i < precision; i++) scale *= 10;
  if (v + 0.5 / (double)scale >= 10.0) exp++;
  return exp;
}

static int ag_rt_trim_float_trailing_zeros(char *tmp, int len) {
  int exp_start = -1;
  int dot = -1;
  int end;
  for (int i = 0; i < len; i++) {
    if (tmp[i] == 'e' || tmp[i] == 'E') {
      exp_start = i;
      break;
    }
  }
  end = exp_start >= 0 ? exp_start : len;
  for (int i = 0; i < end; i++) {
    if (tmp[i] == '.') {
      dot = i;
      break;
    }
  }
  if (dot >= 0) {
    while (end > dot + 1 && tmp[end - 1] == '0') end--;
    if (end == dot + 1) end = dot;
  }
  if (exp_start >= 0) {
    int out = end;
    for (int i = exp_start; i < len; i++) tmp[out++] = tmp[i];
    end = out;
  }
  tmp[end] = 0;
  return end;
}

static void ag_rt_write_general(char *buf, size_t size, int bounded, size_t *pos,
                                double v, int precision, int upper, int alternate, int sign_ch) {
  char tmp[128];
  size_t tmp_pos = 0;
  int exp;
  int len;
  if (ag_rt_double_is_nan(v) || ag_rt_double_is_inf(v)) {
    ag_rt_write_fixed(buf, size, bounded, pos, v, precision, upper, alternate, sign_ch);
    return;
  }
  precision = ag_rt_general_format_precision(precision);
  exp = ag_rt_float_exp10_rounded(v, precision);
  if (exp < -4 || exp >= precision) {
    ag_rt_write_scientific(tmp, sizeof(tmp), 1, &tmp_pos, v, precision - 1, upper,
                           alternate, sign_ch);
  } else {
    int frac_precision = precision - exp - 1;
    if (frac_precision < 0) frac_precision = 0;
    ag_rt_write_fixed(tmp, sizeof(tmp), 1, &tmp_pos, v, frac_precision, upper,
                      alternate, sign_ch);
  }
  ag_rt_finish(tmp, sizeof(tmp), 1, tmp_pos);
  len = alternate ? (int)tmp_pos : ag_rt_trim_float_trailing_zeros(tmp, (int)tmp_pos);
  ag_rt_write_bytes(buf, size, bounded, pos, tmp, len);
}

static void ag_rt_write_general_padded(char *buf, size_t size, int bounded, size_t *pos,
                                       double v, int width, int zero_pad,
                                       int precision, int upper, int alternate, int sign_ch) {
  char tmp[128];
  size_t tmp_pos = 0;
  int len;
  ag_rt_write_general(tmp, sizeof(tmp), 1, &tmp_pos, v, precision, upper, alternate, sign_ch);
  ag_rt_finish(tmp, sizeof(tmp), 1, tmp_pos);
  len = (int)tmp_pos;
  ag_rt_write_float_text_padded(buf, size, bounded, pos, tmp, len, width, zero_pad,
                                !ag_rt_double_is_nan(v) && !ag_rt_double_is_inf(v));
}

static void ag_rt_write_hex_exp_suffix(char *buf, size_t size, int bounded, size_t *pos,
                                       int exp, int upper) {
  int negative = exp < 0;
  unsigned long mag;
  ag_rt_putc(buf, size, bounded, pos, upper ? 'P' : 'p');
  ag_rt_putc(buf, size, bounded, pos, negative ? '-' : '+');
  mag = negative ? (unsigned long)(-exp) : (unsigned long)exp;
  ag_rt_write_udec(buf, size, bounded, pos, mag, 0, 0);
}

static void ag_rt_write_hex_float(char *buf, size_t size, int bounded, size_t *pos,
                                  double v, int precision, int upper, int alternate, int sign_ch) {
  const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
  int exp = 0;
  if (ag_rt_double_is_nan(v) || ag_rt_double_is_inf(v)) {
    ag_rt_write_fixed(buf, size, bounded, pos, v, precision, upper, alternate, sign_ch);
    return;
  }
  if (precision < 0) precision = 13;
  if (precision > 13) precision = 13;
  if (ag_rt_double_is_negative(v)) {
    ag_rt_putc(buf, size, bounded, pos, '-');
    v = -v;
  } else if (sign_ch) {
    ag_rt_putc(buf, size, bounded, pos, sign_ch);
  }
  ag_rt_putc(buf, size, bounded, pos, '0');
  ag_rt_putc(buf, size, bounded, pos, upper ? 'X' : 'x');
  if (v == 0.0) {
    ag_rt_putc(buf, size, bounded, pos, '0');
    if (precision > 0 || alternate) {
      ag_rt_putc(buf, size, bounded, pos, '.');
    }
    if (precision > 0) {
      for (int i = 0; i < precision; i++) ag_rt_putc(buf, size, bounded, pos, '0');
    }
    ag_rt_write_hex_exp_suffix(buf, size, bounded, pos, 0, upper);
    return;
  }
  while (v >= 2.0) {
    v = v / 2.0;
    exp++;
  }
  while (v < 1.0) {
    v = v * 2.0;
    exp--;
  }
  unsigned long scale = 1;
  for (int i = 0; i < precision; i++) scale *= 16;
  double frac_units_d = (v - 1.0) * (double)scale + 0.5;
  unsigned long frac_units = (unsigned long)frac_units_d;
  if (frac_units >= scale) {
    frac_units = 0;
    exp++;
  }
  ag_rt_putc(buf, size, bounded, pos, '1');
  if (precision > 0 || alternate) {
    unsigned long div = scale / 16;
    ag_rt_putc(buf, size, bounded, pos, '.');
    if (precision == 0) {
      ag_rt_write_hex_exp_suffix(buf, size, bounded, pos, exp, upper);
      return;
    }
    for (int i = 0; i < precision; i++) {
      int digit = (int)(frac_units / div);
      ag_rt_putc(buf, size, bounded, pos, digits[digit]);
      frac_units %= div;
      div /= 16;
    }
  }
  ag_rt_write_hex_exp_suffix(buf, size, bounded, pos, exp, upper);
}

static void ag_rt_write_hex_float_padded(char *buf, size_t size, int bounded, size_t *pos,
                                         double v, int width, int zero_pad,
                                         int precision, int upper, int alternate, int sign_ch) {
  char tmp[128];
  size_t tmp_pos = 0;
  ag_rt_write_hex_float(tmp, sizeof(tmp), 1, &tmp_pos, v, precision, upper, alternate, sign_ch);
  ag_rt_finish(tmp, sizeof(tmp), 1, tmp_pos);
  ag_rt_write_float_text_padded(buf, size, bounded, pos, tmp, (int)tmp_pos, width,
                                zero_pad, !ag_rt_double_is_nan(v) && !ag_rt_double_is_inf(v));
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

static long ag_rt_format_read_signed(va_list *ap, int length_hh, int length_h,
                                     int length_l, int length_ll, int length_z,
                                     int length_j, int length_t) {
  if (length_ll || length_j) return va_arg(*ap, long);
  if (length_l || length_z || length_t) return va_arg(*ap, long);
  int v = va_arg(*ap, int);
  if (length_hh) return (long)(signed char)v;
  if (length_h) return (long)(short)v;
  return (long)v;
}

static unsigned long ag_rt_format_read_unsigned(va_list *ap, int length_hh, int length_h,
                                                int length_l, int length_ll, int length_z,
                                                int length_j, int length_t) {
  if (length_ll || length_j) return va_arg(*ap, unsigned long);
  if (length_l || length_z || length_t) return va_arg(*ap, unsigned long);
  if (length_hh || length_h) {
    int v = va_arg(*ap, int);
    if (length_hh) return (unsigned long)(unsigned char)v;
    return (unsigned long)(unsigned short)v;
  }
  return (unsigned long)va_arg(*ap, unsigned int);
}

static void *ag_rt_format_read_ptr(va_list *ap) {
  unsigned long raw = va_arg(*ap, unsigned long);
  return (void *)(long)raw;
}

static void ag_rt_format_store_count(va_list *ap, int length_hh, int length_h,
                                     int length_l, int length_ll, int length_z,
                                     int length_j, int length_t, long value) {
  if (length_ll || length_j) {
    long long *out = (long long *)ag_rt_format_read_ptr(ap);
    *out = (long long)value;
  } else if (length_l || length_z || length_t) {
    long *out = (long *)ag_rt_format_read_ptr(ap);
    *out = value;
  } else if (length_hh) {
    signed char *out = (signed char *)ag_rt_format_read_ptr(ap);
    *out = (signed char)value;
  } else if (length_h) {
    short *out = (short *)ag_rt_format_read_ptr(ap);
    *out = (short)value;
  } else {
    int *out = (int *)ag_rt_format_read_ptr(ap);
    *out = (int)value;
  }
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
    int alternate = 0;
    int sign_ch = 0;
    int width = 0;
    int precision = -1;
    for (;;) {
      if (*fmt == '-') {
        left_align = 1;
        fmt++;
      } else if (*fmt == '0') {
        zero_pad = 1;
        fmt++;
      } else if (*fmt == '#') {
        alternate = 1;
        fmt++;
      } else if (*fmt == '+') {
        sign_ch = '+';
        fmt++;
      } else if (*fmt == ' ') {
        if (!sign_ch) sign_ch = ' ';
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
    int int_zero_pad = precision >= 0 ? 0 : zero_pad;

    int length_z = 0;
    int length_j = 0;
    int length_t = 0;
    int length_h = 0;
    int length_hh = 0;
    int length_l = 0;
    int length_ll = 0;
    int length_L = 0;
    if (*fmt == 'z') {
      length_z = 1;
      fmt++;
    } else if (*fmt == 'j') {
      length_j = 1;
      fmt++;
    } else if (*fmt == 't') {
      length_t = 1;
      fmt++;
    } else if (*fmt == 'h') {
      length_h = 1;
      fmt++;
      if (*fmt == 'h') {
        length_hh = 1;
        fmt++;
      }
    } else if (*fmt == 'l') {
      length_l = 1;
      fmt++;
      if (*fmt == 'l') {
        length_ll = 1;
        fmt++;
      }
    } else if (*fmt == 'L') {
      length_L = 1;
      fmt++;
    }

    if (*fmt == 'd' || *fmt == 'i') {
      long v = ag_rt_format_read_signed(&ap, length_hh, length_h, length_l, length_ll,
                                        length_z, length_j, length_t);
      if (left_align) {
        char tmp[64];
        size_t tmp_pos = 0;
        ag_rt_write_idec(tmp, sizeof(tmp), 1, &tmp_pos, v, 0, 0, precision, sign_ch);
        ag_rt_finish(tmp, sizeof(tmp), 1, tmp_pos);
        ag_rt_write_bytes(buf, size, bounded, &pos, tmp, (int)tmp_pos);
        ag_rt_write_spaces(buf, size, bounded, &pos, width - (int)tmp_pos);
      } else {
        ag_rt_write_idec(buf, size, bounded, &pos, v, width, int_zero_pad, precision, sign_ch);
      }
      fmt++;
    } else if (*fmt == 'u') {
      unsigned long v = ag_rt_format_read_unsigned(&ap, length_hh, length_h, length_l,
                                                   length_ll, length_z, length_j, length_t);
      if (left_align) {
        char tmp[64];
        size_t tmp_pos = 0;
        ag_rt_write_udec_prec(tmp, sizeof(tmp), 1, &tmp_pos, v, 0, 0, precision);
        ag_rt_finish(tmp, sizeof(tmp), 1, tmp_pos);
        ag_rt_write_bytes(buf, size, bounded, &pos, tmp, (int)tmp_pos);
        ag_rt_write_spaces(buf, size, bounded, &pos, width - (int)tmp_pos);
      } else {
        ag_rt_write_udec_prec(buf, size, bounded, &pos, v, width, int_zero_pad, precision);
      }
      fmt++;
    } else if (*fmt == 'x' || *fmt == 'X' || *fmt == 'o') {
      int upper = *fmt == 'X';
      unsigned int base = *fmt == 'o' ? 8u : 16u;
      unsigned long v = ag_rt_format_read_unsigned(&ap, length_hh, length_h, length_l,
                                                   length_ll, length_z, length_j, length_t);
      if (left_align) {
        char tmp[64];
        size_t tmp_pos = 0;
        ag_rt_write_uint_base(tmp, sizeof(tmp), 1, &tmp_pos, v, base, upper, 0, 0,
                              precision, alternate);
        ag_rt_finish(tmp, sizeof(tmp), 1, tmp_pos);
        ag_rt_write_bytes(buf, size, bounded, &pos, tmp, (int)tmp_pos);
        ag_rt_write_spaces(buf, size, bounded, &pos, width - (int)tmp_pos);
      } else {
        ag_rt_write_uint_base(buf, size, bounded, &pos, v, base, upper, width, int_zero_pad,
                              precision, alternate);
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
    } else if (*fmt == 'n') {
      ag_rt_format_store_count(&ap, length_hh, length_h, length_l, length_ll, length_z,
                               length_j, length_t, (long)pos);
      fmt++;
    } else if (*fmt == 'f' || *fmt == 'F' || *fmt == 'e' || *fmt == 'E' ||
               *fmt == 'g' || *fmt == 'G' || *fmt == 'a' || *fmt == 'A') {
      int scientific = *fmt == 'e' || *fmt == 'E';
      int general = *fmt == 'g' || *fmt == 'G';
      int hex_float = *fmt == 'a' || *fmt == 'A';
      int upper = *fmt == 'F' || *fmt == 'E' || *fmt == 'G' || *fmt == 'A';
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
        if (hex_float) {
          ag_rt_write_hex_float(tmp, sizeof(tmp), 1, &tmp_pos, v, precision, upper,
                                alternate, sign_ch);
        } else if (general) {
          ag_rt_write_general(tmp, sizeof(tmp), 1, &tmp_pos, v, precision, upper,
                              alternate, sign_ch);
        } else if (scientific) {
          ag_rt_write_scientific(tmp, sizeof(tmp), 1, &tmp_pos, v, precision, upper,
                                 alternate, sign_ch);
        } else {
          ag_rt_write_fixed(tmp, sizeof(tmp), 1, &tmp_pos, v, precision, upper,
                            alternate, sign_ch);
        }
        ag_rt_finish(tmp, sizeof(tmp), 1, tmp_pos);
        ag_rt_write_bytes(buf, size, bounded, &pos, tmp, (int)tmp_pos);
        ag_rt_write_spaces(buf, size, bounded, &pos, width - (int)tmp_pos);
      } else if (hex_float) {
        ag_rt_write_hex_float_padded(buf, size, bounded, &pos, v, width, zero_pad,
                                     precision, upper, alternate, sign_ch);
      } else if (general) {
        ag_rt_write_general_padded(buf, size, bounded, &pos, v, width, zero_pad,
                                   precision, upper, alternate, sign_ch);
      } else if (scientific) {
        ag_rt_write_scientific_padded(buf, size, bounded, &pos, v, width, zero_pad,
                                      precision, upper, alternate, sign_ch);
      } else {
        ag_rt_write_fixed_padded(buf, size, bounded, &pos, v, width, zero_pad, precision,
                                 upper, alternate, sign_ch);
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

static int ag_rt_scan_is_space(int ch) {
  return ch == ' ' || ch == '\f' || ch == '\n' || ch == '\r' || ch == '\t' || ch == '\v';
}

static int ag_rt_scan_digit_value(int ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return -1;
}

static int ag_rt_scan_ascii_lower(int ch) {
  if (ch >= 'A' && ch <= 'Z') return ch - 'A' + 'a';
  return ch;
}

static int ag_rt_scan_match_lit(const char *p, int width, const char *lit) {
  int n = 0;
  while (lit[n]) {
    if (width == 0) return 0;
    if (ag_rt_scan_ascii_lower((unsigned char)p[n]) != lit[n]) return 0;
    n++;
    if (width > 0) width--;
  }
  return n;
}

static int ag_rt_wscan_match_lit(int *p, int width, const char *lit) {
  int n = 0;
  while (lit[n]) {
    if (width == 0) return 0;
    if (ag_rt_scan_ascii_lower(p[n]) != lit[n]) return 0;
    n++;
    if (width > 0) width--;
  }
  return n;
}

static int ag_rt_scan_nan_payload_len(const char *p, int width) {
  int n = 0;
  if (width == 0 || *p != '(') return 0;
  p++;
  n++;
  if (width > 0) width--;
  while (width != 0 && *p && *p != ')') {
    p++;
    n++;
    if (width > 0) width--;
  }
  if (width != 0 && *p == ')') return n + 1;
  return 0;
}

static int ag_rt_wscan_nan_payload_len(int *p, int width) {
  int n = 0;
  if (width == 0 || *p != '(') return 0;
  p++;
  n++;
  if (width > 0) width--;
  while (width != 0 && *p && *p != ')') {
    p++;
    n++;
    if (width > 0) width--;
  }
  if (width != 0 && *p == ')') return n + 1;
  return 0;
}

static double ag_rt_scan_inf_value(void) {
  double zero = 0.0;
  return 1.0 / zero;
}

static double ag_rt_scan_nan_value(void) {
  double zero = 0.0;
  return zero / zero;
}

static void ag_rt_scan_store_signed(va_list *ap, int length_hh, int length_h, int length_l,
                                    int length_ll, long value) {
  if (length_ll || length_l) {
    long *out = va_arg(*ap, long *);
    *out = value;
  } else if (length_hh) {
    signed char *out = va_arg(*ap, signed char *);
    *out = (signed char)value;
  } else if (length_h) {
    short *out = va_arg(*ap, short *);
    *out = (short)value;
  } else {
    int *out = va_arg(*ap, int *);
    *out = (int)value;
  }
}

static void ag_rt_scan_store_unsigned(va_list *ap, int length_hh, int length_h, int length_l,
                                      int length_ll, unsigned long value) {
  if (length_ll || length_l) {
    unsigned long *out = va_arg(*ap, unsigned long *);
    *out = value;
  } else if (length_hh) {
    unsigned char *out = va_arg(*ap, unsigned char *);
    *out = (unsigned char)value;
  } else if (length_h) {
    unsigned short *out = va_arg(*ap, unsigned short *);
    *out = (unsigned short)value;
  } else {
    unsigned int *out = va_arg(*ap, unsigned int *);
    *out = (unsigned int)value;
  }
}

static void ag_rt_scan_store_count(va_list *ap, int length_hh, int length_h, int length_l,
                                   int length_ll, long value) {
  if (length_ll || length_l) {
    long *out = va_arg(*ap, long *);
    *out = value;
  } else if (length_hh) {
    signed char *out = va_arg(*ap, signed char *);
    *out = (signed char)value;
  } else if (length_h) {
    short *out = va_arg(*ap, short *);
    *out = (short)value;
  } else {
    int *out = va_arg(*ap, int *);
    *out = (int)value;
  }
}

static void *ag_rt_scan_va_arg_ptr(va_list *ap) {
  unsigned long raw = va_arg(*ap, unsigned long);
  return (void *)(long)raw;
}

static int ag_rt_scan_integer(const char **pp, int width, int base, unsigned long *out,
                              int *out_negative) {
  const char *p = *pp;
  const char *start;
  int negative = 0;
  int any = 0;
  unsigned long value = 0;
  while (ag_rt_scan_is_space((unsigned char)*p)) p++;
  start = p;
  if (width != 0 && (*p == '+' || *p == '-')) {
    negative = *p == '-';
    p++;
    if (width > 0) width--;
  }
  if (base == 0) {
    if (width != 0 && p[0] == '0' && (width < 0 || width > 1) &&
        (p[1] == 'x' || p[1] == 'X')) {
      base = 16;
      p += 2;
      if (width > 0) width -= 2;
    } else if (width != 0 && p[0] == '0') {
      base = 8;
    } else {
      base = 10;
    }
  } else if (base == 16 && width != 0 && p[0] == '0' && (width < 0 || width > 1) &&
             (p[1] == 'x' || p[1] == 'X')) {
    p += 2;
    if (width > 0) width -= 2;
  }
  while (width != 0) {
    int digit = ag_rt_scan_digit_value((unsigned char)*p);
    if (digit < 0 || digit >= base) break;
    value = value * (unsigned long)base + (unsigned long)digit;
    p++;
    any = 1;
    if (width > 0) width--;
  }
  if (!any) {
    *pp = start;
    return 0;
  }
  *pp = p;
  *out = value;
  *out_negative = negative;
  return 1;
}

static int ag_rt_scan_float(const char **pp, int width, double *out) {
  const char *p = *pp;
  const char *start;
  const char *after_sign;
  int width_after_sign;
  double sign = 1.0;
  double value = 0.0;
  int have_digit = 0;
  while (ag_rt_scan_is_space((unsigned char)*p)) p++;
  start = p;
  if (width != 0 && (*p == '-' || *p == '+')) {
    if (*p == '-') sign = -1.0;
    p++;
    if (width > 0) width--;
  }
  after_sign = p;
  width_after_sign = width;

  {
    int special_len = ag_rt_scan_match_lit(p, width, "infinity");
    if (special_len == 0) special_len = ag_rt_scan_match_lit(p, width, "inf");
    if (special_len != 0) {
      *pp = p + special_len;
      *out = sign * ag_rt_scan_inf_value();
      return 1;
    }
    special_len = ag_rt_scan_match_lit(p, width, "nan");
    if (special_len != 0) {
      int payload_len = ag_rt_scan_nan_payload_len(p + special_len,
                                                   width < 0 ? -1 : width - special_len);
      *pp = p + special_len + payload_len;
      *out = ag_rt_scan_nan_value();
      return 1;
    }
  }

  if (width != 0 && p[0] == '0' && (width < 0 || width > 1) &&
      (p[1] == 'x' || p[1] == 'X')) {
    const char *hp = p + 2;
    int hwidth = width;
    double hvalue = 0.0;
    int hhave = 0;
    if (hwidth > 0) hwidth -= 2;
    while (hwidth != 0) {
      int digit = ag_rt_scan_digit_value((unsigned char)*hp);
      if (digit < 0 || digit >= 16) break;
      hhave = 1;
      hvalue = hvalue * 16.0 + (double)digit;
      hp++;
      if (hwidth > 0) hwidth--;
    }
    if (hwidth != 0 && ag_rt_is_decimal_point((unsigned char)*hp)) {
      double place = 1.0 / 16.0;
      hp++;
      if (hwidth > 0) hwidth--;
      while (hwidth != 0) {
        int digit = ag_rt_scan_digit_value((unsigned char)*hp);
        if (digit < 0 || digit >= 16) break;
        hhave = 1;
        hvalue = hvalue + (double)digit * place;
        place = place / 16.0;
        hp++;
        if (hwidth > 0) hwidth--;
      }
    }
    if (hhave && hwidth != 0 && (*hp == 'p' || *hp == 'P')) {
      const char *exp_p = hp + 1;
      int exp_width = hwidth;
      int exp_sign = 1;
      int exp = 0;
      int have_exp = 0;
      if (exp_width > 0) exp_width--;
      if (exp_width != 0 && (*exp_p == '-' || *exp_p == '+')) {
        if (*exp_p == '-') exp_sign = -1;
        exp_p++;
        if (exp_width > 0) exp_width--;
      }
      while (exp_width != 0 && *exp_p >= '0' && *exp_p <= '9') {
        have_exp = 1;
        exp = exp * 10 + (*exp_p - '0');
        exp_p++;
        if (exp_width > 0) exp_width--;
      }
      if (have_exp) {
        while (exp > 0) {
          hvalue = exp_sign < 0 ? hvalue / 2.0 : hvalue * 2.0;
          exp--;
        }
        *pp = exp_p;
        *out = sign * hvalue;
        return 1;
      }
    }
    p = after_sign;
    width = width_after_sign;
  }

  while (width != 0 && *p >= '0' && *p <= '9') {
    have_digit = 1;
    value = value * 10.0 + (double)(*p - '0');
    p++;
    if (width > 0) width--;
  }
  if (width != 0 && ag_rt_is_decimal_point((unsigned char)*p)) {
    double place = 0.1;
    p++;
    if (width > 0) width--;
    while (width != 0 && *p >= '0' && *p <= '9') {
      have_digit = 1;
      value = value + (double)(*p - '0') * place;
      place = place / 10.0;
      p++;
      if (width > 0) width--;
    }
  }
  if (!have_digit) {
    *pp = p;
    return 0;
  }
  if (width != 0 && (*p == 'e' || *p == 'E')) {
    const char *exp_start = p;
    int exp_width = width;
    int exp_sign = 1;
    int exp = 0;
    int have_exp = 0;
    p++;
    if (width > 0) width--;
    if (width != 0 && (*p == '-' || *p == '+')) {
      if (*p == '-') exp_sign = -1;
      p++;
      if (width > 0) width--;
    }
    while (width != 0 && *p >= '0' && *p <= '9') {
      have_exp = 1;
      exp = exp * 10 + (*p - '0');
      p++;
      if (width > 0) width--;
    }
    if (have_exp) {
      while (exp > 0) {
        value = exp_sign < 0 ? value / 10.0 : value * 10.0;
        exp--;
      }
    } else {
      p = exp_start;
      width = exp_width;
    }
  }
  (void)width;
  *pp = p;
  *out = sign * value;
  return 1;
}

static const char *ag_rt_scan_set_end(const char *set) {
  const char *p = set;
  if (*p == ']') p++;
  while (*p && *p != ']') p++;
  return *p == ']' ? p : 0;
}

static int *ag_rt_wscan_set_end(int *set) {
  int *p = set;
  if (*p == ']') p++;
  while (*p && *p != ']') p++;
  return *p == ']' ? p : 0;
}

static int ag_rt_scan_set_contains(const char *set, const char *end, int invert, int ch) {
  int found = 0;
  const char *p = set;
  if (p < end && *p == ']') {
    if (ch == ']') found = 1;
    p++;
  }
  while (p < end) {
    int first = (unsigned char)*p;
    if (p + 2 < end && p[1] == '-') {
      int last = (unsigned char)p[2];
      if ((first <= last && ch >= first && ch <= last) ||
          (first > last && ch <= first && ch >= last)) {
        found = 1;
      }
      p += 3;
    } else {
      if (ch == first) found = 1;
      p++;
    }
  }
  return invert ? !found : found;
}

static int ag_rt_wscan_set_contains(int *set, int *end, int invert, int ch) {
  int found = 0;
  int *p = set;
  if (p < end && *p == ']') {
    if (ch == ']') found = 1;
    p++;
  }
  while (p < end) {
    int first = *p;
    if (p + 2 < end && p[1] == '-') {
      int last = p[2];
      if ((first <= last && ch >= first && ch <= last) ||
          (first > last && ch <= first && ch >= last)) {
        found = 1;
      }
      p += 3;
    } else {
      if (ch == first) found = 1;
      p++;
    }
  }
  return invert ? !found : found;
}

static long ag_rt_vscan_consumed_out;
static int ag_rt_vscan_input_failure_out;

static int ag_rt_vscan_consumed(long s_addr, long fmt_addr, long ap) {
  char *s;
  char *fmt;
  char *p;
  int assigned = 0;
  int input_failure = 0;
  s = ag_rt_ptr(s_addr);
  fmt = ag_rt_ptr(fmt_addr);
  p = s;
  while (*fmt) {
    if (ag_rt_scan_is_space(*fmt)) {
      while (ag_rt_scan_is_space(*fmt)) fmt++;
      while (ag_rt_scan_is_space(*p)) p++;
      continue;
    }
    if (*fmt != '%') {
      if (*p != *fmt) {
        if (*p == 0) input_failure = 1;
        break;
      }
      p++;
      fmt++;
      continue;
    }
    fmt++;
    if (*fmt == '%') {
      if (*p != '%') {
        if (*p == 0) input_failure = 1;
        break;
      }
      p++;
      fmt++;
      continue;
    }

    int suppress = 0;
    int width = -1;
    int length_hh = 0;
    int length_h = 0;
    int length_l = 0;
    int length_ll = 0;
    int length_L = 0;
    if (*fmt == '*') {
      suppress = 1;
      fmt++;
    }
    if (*fmt >= '0' && *fmt <= '9') {
      width = 0;
      while (*fmt >= '0' && *fmt <= '9') {
        width = width * 10 + (*fmt - '0');
        fmt++;
      }
      if (width == 0) width = -1;
    }
    if (*fmt == 'h') {
      length_h = 1;
      fmt++;
      if (*fmt == 'h') {
        length_hh = 1;
        length_h = 0;
        fmt++;
      }
    } else if (*fmt == 'l') {
      length_l = 1;
      fmt++;
      if (*fmt == 'l') {
        length_ll = 1;
        fmt++;
      }
    } else if (*fmt == 'L') {
      length_L = 1;
      fmt++;
    }

    if (*fmt == 'd' || *fmt == 'i' || *fmt == 'u' || *fmt == 'x' || *fmt == 'X' ||
        *fmt == 'o') {
      int base = 10;
      unsigned long value = 0;
      int negative = 0;
      if (*fmt == 'i') base = 0;
      if (*fmt == 'x' || *fmt == 'X') base = 16;
      if (*fmt == 'o') base = 8;
      if (!ag_rt_scan_integer(&p, width, base, &value, &negative)) {
        if (*p == 0) input_failure = 1;
        break;
      }
      if (!suppress) {
        if (*fmt == 'd' || *fmt == 'i') {
          long sv = negative ? -(long)value : (long)value;
          ag_rt_scan_store_signed(&ap, length_hh, length_h, length_l, length_ll, sv);
        } else {
          unsigned long uv = negative ? (unsigned long)(-(long)value) : value;
          ag_rt_scan_store_unsigned(&ap, length_hh, length_h, length_l, length_ll, uv);
        }
        assigned++;
      }
      fmt++;
    } else if (*fmt == 'f' || *fmt == 'F' || *fmt == 'e' || *fmt == 'E' ||
               *fmt == 'g' || *fmt == 'G') {
      double value = 0.0;
      if (!ag_rt_scan_float(&p, width, &value)) {
        if (*p == 0) input_failure = 1;
        break;
      }
      if (!suppress) {
        if (length_L) {
          long double *out = (long double *)ag_rt_scan_va_arg_ptr(&ap);
          *out = (long double)value;
        } else if (length_l) {
          double *out = (double *)ag_rt_scan_va_arg_ptr(&ap);
          *out = value;
        } else {
          float *out = (float *)ag_rt_scan_va_arg_ptr(&ap);
          *out = (float)value;
        }
        assigned++;
      }
      fmt++;
    } else if (*fmt == 'p') {
      unsigned long value = 0;
      int negative = 0;
      if (!ag_rt_scan_integer(&p, width, 16, &value, &negative)) {
        if (*p == 0) input_failure = 1;
        break;
      }
      if (!suppress) {
        void **out = va_arg(ap, void **);
        *out = (void *)(long)(negative ? (unsigned long)(-(long)value) : value);
        assigned++;
      }
      fmt++;
    } else if (*fmt == 's') {
      int n = 0;
      while (ag_rt_scan_is_space(*p)) p++;
      if (*p == 0) {
        input_failure = 1;
        break;
      }
      if (length_l) {
        int *out = 0;
        if (!suppress) out = va_arg(ap, int *);
        while (*p && !ag_rt_scan_is_space(*p) && width != 0) {
          if (!suppress) out[n] = (unsigned char)*p;
          p++;
          n++;
          if (width > 0) width--;
        }
        if (!suppress) out[n] = 0;
      } else {
        char *out = 0;
        if (!suppress) out = va_arg(ap, char *);
        while (*p && !ag_rt_scan_is_space(*p) && width != 0) {
          if (!suppress) *(out + n) = *p;
          p++;
          n++;
          if (width > 0) width--;
        }
        if (!suppress) *(out + n) = 0;
      }
      if (n == 0) {
        if (*p == 0) input_failure = 1;
        break;
      }
      if (!suppress) assigned++;
      fmt++;
    } else if (*fmt == 'c') {
      int n = 0;
      if (width < 0) width = 1;
      if (length_l) {
        int *out = 0;
        if (!suppress) out = va_arg(ap, int *);
        while (*p && width > 0) {
          if (!suppress) out[n] = (unsigned char)*p;
          p++;
          n++;
          width--;
        }
      } else {
        char *out = 0;
        if (!suppress) out = va_arg(ap, char *);
        while (*p && width > 0) {
          if (!suppress) *(out + n) = *p;
          p++;
          n++;
          width--;
        }
      }
      if (n == 0) {
        if (*p == 0) input_failure = 1;
        break;
      }
      if (!suppress) assigned++;
      fmt++;
    } else if (*fmt == '[') {
      int n = 0;
      int invert = 0;
      const char *set = fmt + 1;
      const char *set_end;
      if (*set == '^') {
        invert = 1;
        set++;
      }
      set_end = ag_rt_scan_set_end(set);
      if (!set_end) break;
      if (length_l) {
        int *out = 0;
        if (!suppress) out = va_arg(ap, int *);
        while (*p && width != 0 &&
               ag_rt_scan_set_contains(set, set_end, invert, (unsigned char)*p)) {
          if (!suppress) out[n] = (unsigned char)*p;
          p++;
          n++;
          if (width > 0) width--;
        }
        if (!suppress) out[n] = 0;
      } else {
        char *out = 0;
        if (!suppress) out = va_arg(ap, char *);
        while (*p && width != 0 &&
               ag_rt_scan_set_contains(set, set_end, invert, (unsigned char)*p)) {
          if (!suppress) *(out + n) = *p;
          p++;
          n++;
          if (width > 0) width--;
        }
        if (!suppress) *(out + n) = 0;
      }
      if (n == 0) {
        if (*p == 0) input_failure = 1;
        break;
      }
      if (!suppress) assigned++;
      fmt = set_end + 1;
    } else if (*fmt == 'n') {
      if (!suppress) {
        ag_rt_scan_store_count(&ap, length_hh, length_h, length_l, length_ll, (long)(p - s));
      }
      fmt++;
    } else {
      break;
    }
  }
  ag_rt_vscan_consumed_out = (long)(p - s);
  ag_rt_vscan_input_failure_out = input_failure;
  if (assigned == 0 && input_failure) return -1;
  return assigned;
}

static int ag_rt_vscan(const char *s, const char *fmt, va_list ap) {
  return ag_rt_vscan_consumed((long)s, (long)fmt, ap);
}

static int ag_rt_wscan_integer(int **pp, int width, int base, unsigned long *out,
                               int *out_negative) {
  int *p = *pp;
  int *start;
  int negative = 0;
  int any = 0;
  unsigned long value = 0;
  while (ag_rt_scan_is_space(*p)) p++;
  start = p;
  if (width != 0 && (*p == '+' || *p == '-')) {
    negative = *p == '-';
    p++;
    if (width > 0) width--;
  }
  if (base == 0) {
    if (width != 0 && p[0] == '0' && (width < 0 || width > 1) &&
        (p[1] == 'x' || p[1] == 'X')) {
      base = 16;
      p += 2;
      if (width > 0) width -= 2;
    } else if (width != 0 && p[0] == '0') {
      base = 8;
    } else {
      base = 10;
    }
  } else if (base == 16 && width != 0 && p[0] == '0' && (width < 0 || width > 1) &&
             (p[1] == 'x' || p[1] == 'X')) {
    p += 2;
    if (width > 0) width -= 2;
  }
  while (width != 0) {
    int digit = ag_rt_scan_digit_value(*p);
    if (digit < 0 || digit >= base) break;
    value = value * (unsigned long)base + (unsigned long)digit;
    p++;
    any = 1;
    if (width > 0) width--;
  }
  if (!any) {
    *pp = start;
    return 0;
  }
  *pp = p;
  *out = value;
  *out_negative = negative;
  return 1;
}

static int ag_rt_wscan_float(int **pp, int width, double *out) {
  int *p = *pp;
  int *after_sign;
  int width_after_sign;
  double sign = 1.0;
  double value = 0.0;
  int have_digit = 0;
  while (ag_rt_scan_is_space(*p)) p++;
  if (width != 0 && (*p == '-' || *p == '+')) {
    if (*p == '-') sign = -1.0;
    p++;
    if (width > 0) width--;
  }
  after_sign = p;
  width_after_sign = width;

  {
    int special_len = ag_rt_wscan_match_lit(p, width, "infinity");
    if (special_len == 0) special_len = ag_rt_wscan_match_lit(p, width, "inf");
    if (special_len != 0) {
      *pp = p + special_len;
      *out = sign * ag_rt_scan_inf_value();
      return 1;
    }
    special_len = ag_rt_wscan_match_lit(p, width, "nan");
    if (special_len != 0) {
      int payload_len = ag_rt_wscan_nan_payload_len(p + special_len,
                                                    width < 0 ? -1 : width - special_len);
      *pp = p + special_len + payload_len;
      *out = ag_rt_scan_nan_value();
      return 1;
    }
  }

  if (width != 0 && p[0] == '0' && (width < 0 || width > 1) &&
      (p[1] == 'x' || p[1] == 'X')) {
    int *hp = p + 2;
    int hwidth = width;
    double hvalue = 0.0;
    int hhave = 0;
    if (hwidth > 0) hwidth -= 2;
    while (hwidth != 0) {
      int digit = ag_rt_scan_digit_value(*hp);
      if (digit < 0 || digit >= 16) break;
      hhave = 1;
      hvalue = hvalue * 16.0 + (double)digit;
      hp++;
      if (hwidth > 0) hwidth--;
    }
    if (hwidth != 0 && ag_rt_is_decimal_point(*hp)) {
      double place = 1.0 / 16.0;
      hp++;
      if (hwidth > 0) hwidth--;
      while (hwidth != 0) {
        int digit = ag_rt_scan_digit_value(*hp);
        if (digit < 0 || digit >= 16) break;
        hhave = 1;
        hvalue = hvalue + (double)digit * place;
        place = place / 16.0;
        hp++;
        if (hwidth > 0) hwidth--;
      }
    }
    if (hhave && hwidth != 0 && (*hp == 'p' || *hp == 'P')) {
      int *exp_p = hp + 1;
      int exp_width = hwidth;
      int exp_sign = 1;
      int exp = 0;
      int have_exp = 0;
      if (exp_width > 0) exp_width--;
      if (exp_width != 0 && (*exp_p == '-' || *exp_p == '+')) {
        if (*exp_p == '-') exp_sign = -1;
        exp_p++;
        if (exp_width > 0) exp_width--;
      }
      while (exp_width != 0 && *exp_p >= '0' && *exp_p <= '9') {
        have_exp = 1;
        exp = exp * 10 + (*exp_p - '0');
        exp_p++;
        if (exp_width > 0) exp_width--;
      }
      if (have_exp) {
        while (exp > 0) {
          hvalue = exp_sign < 0 ? hvalue / 2.0 : hvalue * 2.0;
          exp--;
        }
        *pp = exp_p;
        *out = sign * hvalue;
        return 1;
      }
    }
    p = after_sign;
    width = width_after_sign;
  }

  while (width != 0 && *p >= '0' && *p <= '9') {
    have_digit = 1;
    value = value * 10.0 + (double)(*p - '0');
    p++;
    if (width > 0) width--;
  }
  if (width != 0 && ag_rt_is_decimal_point(*p)) {
    double place = 0.1;
    p++;
    if (width > 0) width--;
    while (width != 0 && *p >= '0' && *p <= '9') {
      have_digit = 1;
      value = value + (double)(*p - '0') * place;
      place = place / 10.0;
      p++;
      if (width > 0) width--;
    }
  }
  if (!have_digit) {
    *pp = p;
    return 0;
  }
  if (width != 0 && (*p == 'e' || *p == 'E')) {
    int *exp_start = p;
    int exp_width = width;
    int exp_sign = 1;
    int exp = 0;
    int have_exp = 0;
    p++;
    if (width > 0) width--;
    if (width != 0 && (*p == '-' || *p == '+')) {
      if (*p == '-') exp_sign = -1;
      p++;
      if (width > 0) width--;
    }
    while (width != 0 && *p >= '0' && *p <= '9') {
      have_exp = 1;
      exp = exp * 10 + (*p - '0');
      p++;
      if (width > 0) width--;
    }
    if (have_exp) {
      while (exp > 0) {
        value = exp_sign < 0 ? value / 10.0 : value * 10.0;
        exp--;
      }
    } else {
      p = exp_start;
      width = exp_width;
    }
  }
  (void)width;
  *pp = p;
  *out = sign * value;
  return 1;
}

static int ag_rt_vwscan(int *s, int *fmt, va_list ap) {
  int *p = s;
  int assigned = 0;
  int input_failure = 0;
  while (*fmt) {
    if (ag_rt_scan_is_space(*fmt)) {
      while (ag_rt_scan_is_space(*fmt)) fmt++;
      while (ag_rt_scan_is_space(*p)) p++;
      continue;
    }
    if (*fmt != '%') {
      if (*p != *fmt) {
        if (*p == 0) input_failure = 1;
        break;
      }
      p++;
      fmt++;
      continue;
    }
    fmt++;
    if (*fmt == '%') {
      if (*p != '%') {
        if (*p == 0) input_failure = 1;
        break;
      }
      p++;
      fmt++;
      continue;
    }

    int suppress = 0;
    int width = -1;
    int length_hh = 0;
    int length_h = 0;
    int length_l = 0;
    int length_ll = 0;
    int length_L = 0;
    if (*fmt == '*') {
      suppress = 1;
      fmt++;
    }
    if (*fmt >= '0' && *fmt <= '9') {
      width = 0;
      while (*fmt >= '0' && *fmt <= '9') {
        width = width * 10 + (*fmt - '0');
        fmt++;
      }
      if (width == 0) width = -1;
    }
    if (*fmt == 'h') {
      length_h = 1;
      fmt++;
      if (*fmt == 'h') {
        length_hh = 1;
        length_h = 0;
        fmt++;
      }
    } else if (*fmt == 'l') {
      length_l = 1;
      fmt++;
      if (*fmt == 'l') {
        length_ll = 1;
        fmt++;
      }
    } else if (*fmt == 'L') {
      length_L = 1;
      fmt++;
    }

    if (*fmt == 'd' || *fmt == 'i' || *fmt == 'u' || *fmt == 'x' || *fmt == 'X' ||
        *fmt == 'o') {
      int base = 10;
      unsigned long value = 0;
      int negative = 0;
      if (*fmt == 'i') base = 0;
      if (*fmt == 'x' || *fmt == 'X') base = 16;
      if (*fmt == 'o') base = 8;
      if (!ag_rt_wscan_integer(&p, width, base, &value, &negative)) {
        if (*p == 0) input_failure = 1;
        break;
      }
      if (!suppress) {
        if (*fmt == 'd' || *fmt == 'i') {
          long sv = negative ? -(long)value : (long)value;
          ag_rt_scan_store_signed(&ap, length_hh, length_h, length_l, length_ll, sv);
        } else {
          unsigned long uv = negative ? (unsigned long)(-(long)value) : value;
          ag_rt_scan_store_unsigned(&ap, length_hh, length_h, length_l, length_ll, uv);
        }
        assigned++;
      }
      fmt++;
    } else if (*fmt == 'f' || *fmt == 'F' || *fmt == 'e' || *fmt == 'E' ||
               *fmt == 'g' || *fmt == 'G') {
      double value = 0.0;
      if (!ag_rt_wscan_float(&p, width, &value)) {
        if (*p == 0) input_failure = 1;
        break;
      }
      if (!suppress) {
        if (length_L) {
          long double *out = (long double *)ag_rt_scan_va_arg_ptr(&ap);
          *out = (long double)value;
        } else if (length_l) {
          double *out = (double *)ag_rt_scan_va_arg_ptr(&ap);
          *out = value;
        } else {
          float *out = (float *)ag_rt_scan_va_arg_ptr(&ap);
          *out = (float)value;
        }
        assigned++;
      }
      fmt++;
    } else if (*fmt == 'p') {
      unsigned long value = 0;
      int negative = 0;
      if (!ag_rt_wscan_integer(&p, width, 16, &value, &negative)) {
        if (*p == 0) input_failure = 1;
        break;
      }
      if (!suppress) {
        void **out = va_arg(ap, void **);
        *out = (void *)(long)(negative ? (unsigned long)(-(long)value) : value);
        assigned++;
      }
      fmt++;
    } else if (*fmt == 's') {
      int n = 0;
      while (ag_rt_scan_is_space(*p)) p++;
      if (*p == 0) {
        input_failure = 1;
        break;
      }
      if (length_l) {
        int *out = 0;
        if (!suppress) out = va_arg(ap, int *);
        while (*p && !ag_rt_scan_is_space(*p) && width != 0) {
          if (!suppress) out[n] = *p;
          p++;
          n++;
          if (width > 0) width--;
        }
        if (!suppress) out[n] = 0;
      } else {
        char *out = 0;
        if (!suppress) out = va_arg(ap, char *);
        while (*p && !ag_rt_scan_is_space(*p) && width != 0) {
          if (!suppress) out[n] = (char)*p;
          p++;
          n++;
          if (width > 0) width--;
        }
        if (!suppress) out[n] = 0;
      }
      if (n == 0) {
        if (*p == 0) input_failure = 1;
        break;
      }
      if (!suppress) assigned++;
      fmt++;
    } else if (*fmt == 'c') {
      int n = 0;
      if (width < 0) width = 1;
      if (length_l) {
        int *out = 0;
        if (!suppress) out = va_arg(ap, int *);
        while (*p && width > 0) {
          if (!suppress) out[n] = *p;
          p++;
          n++;
          width--;
        }
      } else {
        char *out = 0;
        if (!suppress) out = va_arg(ap, char *);
        while (*p && width > 0) {
          if (!suppress) out[n] = (char)*p;
          p++;
          n++;
          width--;
        }
      }
      if (n == 0) {
        if (*p == 0) input_failure = 1;
        break;
      }
      if (!suppress) assigned++;
      fmt++;
    } else if (*fmt == '[') {
      int n = 0;
      int invert = 0;
      int *set = fmt + 1;
      int *set_end;
      if (*set == '^') {
        invert = 1;
        set++;
      }
      set_end = ag_rt_wscan_set_end(set);
      if (!set_end) break;
      if (length_l) {
        int *out = 0;
        if (!suppress) out = va_arg(ap, int *);
        while (*p && width != 0 && ag_rt_wscan_set_contains(set, set_end, invert, *p)) {
          if (!suppress) out[n] = *p;
          p++;
          n++;
          if (width > 0) width--;
        }
        if (!suppress) out[n] = 0;
      } else {
        char *out = 0;
        if (!suppress) out = va_arg(ap, char *);
        while (*p && width != 0 && ag_rt_wscan_set_contains(set, set_end, invert, *p)) {
          if (!suppress) out[n] = (char)*p;
          p++;
          n++;
          if (width > 0) width--;
        }
        if (!suppress) out[n] = 0;
      }
      if (n == 0) {
        if (*p == 0) input_failure = 1;
        break;
      }
      if (!suppress) assigned++;
      fmt = set_end + 1;
    } else if (*fmt == 'n') {
      if (!suppress) {
        ag_rt_scan_store_count(&ap, length_hh, length_h, length_l, length_ll, (long)(p - s));
      }
      fmt++;
    } else {
      break;
    }
  }
  if (assigned == 0 && input_failure) return -1;
  return assigned;
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

int __agc_runtime_vsprintf(long buf_addr, long fmt_addr, long ap_addr) {
  char *buf = (char *)(long)buf_addr;
  char *fmt = (char *)(long)fmt_addr;
  va_list ap = (va_list)(long)ap_addr;
  return ag_rt_vformat(buf, 0, 0, fmt, ap);
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

int __agc_runtime_sscanf(long s_addr, long fmt_addr, ...) {
  char *s = (char *)(long)s_addr;
  char *fmt = (char *)(long)fmt_addr;
  va_list ap;
  va_start(ap, fmt_addr);
  int n = ag_rt_vscan(s, fmt, ap);
  va_end(ap);
  return n;
}

int __agc_runtime_vsscanf(long s_addr, long fmt_addr, long ap_addr) {
  char *s = (char *)(long)s_addr;
  char *fmt = (char *)(long)fmt_addr;
  va_list ap = (va_list)(long)ap_addr;
  return ag_rt_vscan(s, fmt, ap);
}

static int ag_rt_vfscan(long stream_addr, char *fmt, va_list ap) {
  struct ag_rt_file *f = ag_rt_input_stream(stream_addr);
  char *src;
  long len;
  long avail;
  long copied = 0;
  long consumed = 0;
  long advance = 0;
  long i;
  int input_failure = 0;
  int had_ungetc;
  int n;
  static char scan_buf[AG_RT_FILE_BUF_CAP];
  if (!f) {
    ag_rt_set_errno(9);
    return -1;
  }
  if (!ag_rt_file_can_read(f)) {
    f->error = 1;
    ag_rt_set_errno(9);
    return -1;
  }
  src = ag_rt_stream_buf(f);
  len = ag_rt_stream_len(f);
  had_ungetc = f->has_ungetc;
  if (had_ungetc) {
    scan_buf[copied++] = (char)f->ungetc_ch;
  }
  avail = f->pos < len ? len - f->pos : 0;
  if (avail >= AG_RT_FILE_BUF_CAP - copied) avail = AG_RT_FILE_BUF_CAP - copied - 1;
  for (i = 0; i < avail; i++) scan_buf[copied + i] = src[f->pos + i];
  copied += avail;
  scan_buf[copied] = 0;
  n = ag_rt_vscan_consumed((long)scan_buf, (long)fmt, ap);
  consumed = ag_rt_vscan_consumed_out;
  input_failure = ag_rt_vscan_input_failure_out;
  if (consumed > 0) {
    advance = consumed;
    if (had_ungetc) advance--;
    if (advance < 0) advance = 0;
    ag_rt_file_set_pos(f, f->pos + advance);
  }
  if (input_failure) f->eof = 1;
  return n;
}

int __agc_runtime_scanf(long fmt_addr, ...) {
  char *fmt = (char *)(long)fmt_addr;
  va_list ap;
  va_start(ap, fmt_addr);
  int n = ag_rt_vfscan((long)&ag_rt_file_value, fmt, ap);
  va_end(ap);
  return n;
}

int __agc_runtime_vscanf(long fmt_addr, long ap_addr) {
  char *fmt = (char *)(long)fmt_addr;
  va_list ap = (va_list)(long)ap_addr;
  return ag_rt_vfscan((long)&ag_rt_file_value, fmt, ap);
}

int __agc_runtime_fscanf(long stream_addr, long fmt_addr, ...) {
  char *fmt = (char *)(long)fmt_addr;
  va_list ap;
  va_start(ap, fmt_addr);
  int n = ag_rt_vfscan(stream_addr, fmt, ap);
  va_end(ap);
  return n;
}

int __agc_runtime_vfscanf(long stream_addr, long fmt_addr, long ap_addr) {
  char *fmt = (char *)(long)fmt_addr;
  va_list ap = (va_list)(long)ap_addr;
  return ag_rt_vfscan(stream_addr, fmt, ap);
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

int __agc_runtime_vprintf(long fmt_addr, long ap_addr) {
  char *fmt = (char *)(long)fmt_addr;
  va_list ap = (va_list)(long)ap_addr;
  char buf[1024];
  int n = ag_rt_vformat(buf, sizeof(buf), 1, fmt, ap);
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
    if (!f) {
      ag_rt_set_errno(9);
      return -1;
    }
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
  int *buf = (int *)ag_rt_ptr(buf_addr);
  int *fmt = (int *)ag_rt_ptr(fmt_addr);
  va_list ap;
  int n;
  va_start(ap, fmt_addr);
  n = ag_rt_vwformat(buf, size, fmt, ap);
  va_end(ap);
  return n;
}

int __agc_runtime_swscanf(long s_addr, long fmt_addr, ...) {
  int *s = (int *)ag_rt_ptr(s_addr);
  int *fmt = (int *)ag_rt_ptr(fmt_addr);
  va_list ap;
  int n;
  va_start(ap, fmt_addr);
  n = ag_rt_vwscan(s, fmt, ap);
  va_end(ap);
  return n;
}

void __agc_runtime___assert_rtn(long func_addr, long file_addr, int line, long expr_addr) {
  (void)func_addr;
  (void)file_addr;
  (void)line;
  (void)expr_addr;
  for (;;) {
  }
}
