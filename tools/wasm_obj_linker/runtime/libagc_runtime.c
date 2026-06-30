#include <stdarg.h>
#include <stddef.h>

static void ag_rt_putc(char *buf, size_t size, int bounded, size_t *pos, int ch) {
  if (!bounded || (long)(*pos + 1) < (long)size) buf[(long)*pos] = (char)ch;
  *pos = *pos + 1;
}

static void ag_rt_finish(char *buf, size_t size, int bounded, size_t pos) {
  if (!bounded) {
    buf[(long)pos] = 0;
  } else if (size != 0) {
    buf[(long)((long)pos < (long)size ? pos : size - 1)] = 0;
  }
}

static void ag_rt_write_str(char *buf, size_t size, int bounded, size_t *pos, const char *s) {
  if (!s) s = "(null)";
  while (*s) {
    ag_rt_putc(buf, size, bounded, pos, *s);
    s++;
  }
}

static char *ag_rt_ptr(long addr) {
  return (char *)addr;
}

long __agc_runtime_strlen(long s_addr) {
  char *s = ag_rt_ptr(s_addr);
  long n = 0;
  while (s[n]) n++;
  return n;
}

int __agc_runtime_strcmp(long a_addr, long b_addr) {
  unsigned char *a = (unsigned char *)ag_rt_ptr(a_addr);
  unsigned char *b = (unsigned char *)ag_rt_ptr(b_addr);
  long i = 0;
  while (a[i] && a[i] == b[i]) i++;
  return (int)a[i] - (int)b[i];
}

long __agc_runtime_memset(long dst_addr, int ch, long n) {
  unsigned char *dst = (unsigned char *)ag_rt_ptr(dst_addr);
  long i = 0;
  while (i < n) dst[i++] = (unsigned char)ch;
  return dst_addr;
}

long __agc_runtime_memcpy(long dst_addr, long src_addr, long n) {
  unsigned char *dst = (unsigned char *)ag_rt_ptr(dst_addr);
  unsigned char *src = (unsigned char *)ag_rt_ptr(src_addr);
  long i = 0;
  while (i < n) {
    dst[i] = src[i];
    i++;
  }
  return dst_addr;
}

int __agc_runtime_abs(int x) {
  return x < 0 ? -x : x;
}

int __agc_runtime_isdigit(int c) {
  return c >= '0' && c <= '9';
}

int __agc_runtime_isalpha(int c) {
  int lower = c | 32;
  return lower >= 'a' && lower <= 'z';
}

int __agc_runtime_toupper(int c) {
  return c >= 'a' && c <= 'z' ? c - 32 : c;
}

int __agc_runtime_atoi(long s_addr) {
  char *s = ag_rt_ptr(s_addr);
  int sign = 1;
  int acc = 0;
  while (*s == ' ') s++;
  if (*s == '-') {
    sign = -1;
    s++;
  } else if (*s == '+') {
    s++;
  }
  while (*s >= '0' && *s <= '9') {
    acc = acc * 10 + (*s - '0');
    s++;
  }
  return acc * sign;
}

long __agc_runtime_strcpy(long dst_addr, long src_addr) {
  char *dst = ag_rt_ptr(dst_addr);
  char *src = ag_rt_ptr(src_addr);
  long i = 0;
  do {
    dst[i] = src[i];
  } while (src[i++] != 0);
  return dst_addr;
}

long __agc_runtime_strncpy(long dst_addr, long src_addr, long n) {
  char *dst = ag_rt_ptr(dst_addr);
  char *src = ag_rt_ptr(src_addr);
  long i = 0;
  int ended = 0;
  while (i < n) {
    char c = ended ? 0 : src[i];
    dst[i] = c;
    if (c == 0) ended = 1;
    i++;
  }
  return dst_addr;
}

long __agc_runtime_strcat(long dst_addr, long src_addr) {
  char *dst = ag_rt_ptr(dst_addr);
  char *src = ag_rt_ptr(src_addr);
  long end = 0;
  long i = 0;
  while (dst[end]) end++;
  do {
    dst[end + i] = src[i];
  } while (src[i++] != 0);
  return dst_addr;
}

int __agc_runtime_strncmp(long a_addr, long b_addr, long n) {
  unsigned char *a = (unsigned char *)ag_rt_ptr(a_addr);
  unsigned char *b = (unsigned char *)ag_rt_ptr(b_addr);
  long i = 0;
  while (i < n) {
    if (a[i] != b[i]) return (int)a[i] - (int)b[i];
    if (a[i] == 0) return 0;
    i++;
  }
  return 0;
}

int __agc_runtime_memcmp(long a_addr, long b_addr, long n) {
  unsigned char *a = (unsigned char *)ag_rt_ptr(a_addr);
  unsigned char *b = (unsigned char *)ag_rt_ptr(b_addr);
  long i = 0;
  while (i < n) {
    if (a[i] != b[i]) return (int)a[i] - (int)b[i];
    i++;
  }
  return 0;
}

long __agc_runtime_strchr(long s_addr, int ch) {
  char *s = ag_rt_ptr(s_addr);
  int needle = ch & 255;
  long i = 0;
  for (;;) {
    if (((int)s[i] & 255) == needle) return s_addr + i;
    if (s[i] == 0) return 0;
    i++;
  }
  return 0;
}

long __agc_runtime_strrchr(long s_addr, int ch) {
  char *s = ag_rt_ptr(s_addr);
  int needle = ch & 255;
  long found = 0;
  long i = 0;
  for (;;) {
    if (((int)s[i] & 255) == needle) found = s_addr + i;
    if (s[i] == 0) return found;
    i++;
  }
  return 0;
}

int __agc_runtime_putchar(int c) {
  return c;
}

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

    int zero_pad = 0;
    int width = 0;
    if (*fmt == '0') {
      zero_pad = 1;
      fmt++;
    }
    while ((int)*fmt >= '0' && (int)*fmt <= '9') {
      width = width * 10 + (*fmt - '0');
      fmt++;
    }

    int length_z = 0;
    if (*fmt == 'z') {
      length_z = 1;
      fmt++;
    }

    if (*fmt == 'd') {
      ag_rt_write_idec(buf, size, bounded, &pos, va_arg(ap, int), width, zero_pad);
      fmt++;
    } else if (*fmt == 'u') {
      unsigned long v = length_z ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
      ag_rt_write_udec(buf, size, bounded, &pos, v, width, zero_pad);
      fmt++;
    } else if (*fmt == 's') {
      ag_rt_write_str(buf, size, bounded, &pos, va_arg(ap, char *));
      fmt++;
    } else if (*fmt == 'c') {
      ag_rt_putc(buf, size, bounded, &pos, va_arg(ap, int));
      fmt++;
    } else {
      ag_rt_putc(buf, size, bounded, &pos, '%');
      if (*fmt) ag_rt_putc(buf, size, bounded, &pos, *fmt++);
    }
  }
  ag_rt_finish(buf, size, bounded, pos);
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

int __agc_runtime_sprintf(long buf_addr, long fmt_addr, ...) {
  char *buf = (char *)(long)buf_addr;
  char *fmt = (char *)(long)fmt_addr;
  va_list ap;
  va_start(ap, fmt_addr);
  int n = ag_rt_vformat(buf, 0, 0, fmt, ap);
  va_end(ap);
  return n;
}
