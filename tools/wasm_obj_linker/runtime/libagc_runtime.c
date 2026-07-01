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

static long ag_rt_heap = 32768;
static char ag_rt_locale_c[] = "C";
static char ag_rt_decimal_point[] = ".";
static char ag_rt_file_buf[512];
static long ag_rt_file_len = 0;
void *__stdinp;
void *__stdoutp;
void *__stderrp;

struct ag_rt_file {
  long pos;
  int write_mode;
};

struct ag_rt_lconv {
  char *decimal_point;
};

static struct ag_rt_lconv ag_rt_lconv_value = {ag_rt_decimal_point};
static struct ag_rt_file ag_rt_file_value;

long __agc_runtime_malloc(long size) {
  long aligned = (size + 7) & -8;
  long p = ag_rt_heap;
  ag_rt_heap = ag_rt_heap + aligned;
  return p;
}

void __agc_runtime_free(long ptr) {
  (void)ptr;
}

long __agc_runtime_calloc(long nmemb, long size) {
  long n = nmemb * size;
  long p = __agc_runtime_malloc(n);
  char *dst = ag_rt_ptr(p);
  long i = 0;
  while (i < n) dst[i++] = 0;
  return p;
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

long __agc_runtime_imaxabs(long x) {
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

long __agc_runtime_wcslen(long s_addr) {
  int *s = (int *)ag_rt_ptr(s_addr);
  long n = 0;
  while (s[n]) n++;
  return n;
}

long __agc_runtime_wcscpy(long dst_addr, long src_addr) {
  int *dst = (int *)ag_rt_ptr(dst_addr);
  int *src = (int *)ag_rt_ptr(src_addr);
  long i = 0;
  do {
    dst[i] = src[i];
  } while (src[i++] != 0);
  return dst_addr;
}

int __agc_runtime_wcscmp(long a_addr, long b_addr) {
  int *a = (int *)ag_rt_ptr(a_addr);
  int *b = (int *)ag_rt_ptr(b_addr);
  long i = 0;
  while (a[i] && a[i] == b[i]) i++;
  return a[i] - b[i];
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

long __agc_runtime_fopen(long path_addr, long mode_addr) {
  (void)path_addr;
  char *mode = ag_rt_ptr(mode_addr);
  ag_rt_file_value.pos = 0;
  ag_rt_file_value.write_mode = mode && mode[0] == 'w';
  if (ag_rt_file_value.write_mode) ag_rt_file_len = 0;
  return (long)&ag_rt_file_value;
}

int __agc_runtime_fclose(long stream_addr) {
  (void)stream_addr;
  return 0;
}

long __agc_runtime_fwrite(long ptr_addr, long size, long nmemb, long stream_addr) {
  (void)stream_addr;
  char *src = ag_rt_ptr(ptr_addr);
  long total = size * nmemb;
  long i = 0;
  while (i < total && ag_rt_file_len < (long)sizeof(ag_rt_file_buf)) {
    ag_rt_file_buf[ag_rt_file_len++] = src[i++];
  }
  return size == 0 ? 0 : i / size;
}

long __agc_runtime_fread(long ptr_addr, long size, long nmemb, long stream_addr) {
  struct ag_rt_file *f = (struct ag_rt_file *)ag_rt_ptr(stream_addr);
  char *dst = ag_rt_ptr(ptr_addr);
  long total = size * nmemb;
  long i = 0;
  while (i < total && f->pos < ag_rt_file_len) {
    dst[i++] = ag_rt_file_buf[f->pos++];
  }
  return size == 0 ? 0 : i / size;
}

int __agc_runtime_fgetc(long stream_addr) {
  struct ag_rt_file *f = (struct ag_rt_file *)ag_rt_ptr(stream_addr);
  if (f->pos >= ag_rt_file_len) return -1;
  return (int)(unsigned char)ag_rt_file_buf[f->pos++];
}

int __agc_runtime_getc(long stream_addr) {
  return __agc_runtime_fgetc(stream_addr);
}

long __agc_runtime_fgets(long s_addr, int size, long stream_addr) {
  struct ag_rt_file *f = (struct ag_rt_file *)ag_rt_ptr(stream_addr);
  char *dst = ag_rt_ptr(s_addr);
  int i = 0;
  if (size <= 0 || f->pos >= ag_rt_file_len) return 0;
  while (i + 1 < size && f->pos < ag_rt_file_len) {
    char ch = ag_rt_file_buf[f->pos++];
    dst[i++] = ch;
    if (ch == '\n') break;
  }
  dst[i] = 0;
  return s_addr;
}

int __agc_runtime_feclearexcept(int excepts) {
  (void)excepts;
  return 0;
}

int __agc_runtime_fetestexcept(int excepts) {
  return excepts;
}

long __agc_runtime_setlocale(int category, long locale_addr) {
  (void)category;
  (void)locale_addr;
  return (long)ag_rt_locale_c;
}

long __agc_runtime_localeconv(void) {
  return (long)&ag_rt_lconv_value;
}

double __agc_runtime_sqrt(double x) {
  if (x <= 0.0) return 0.0;
  double g = x > 1.0 ? x : 1.0;
  for (int i = 0; i < 12; i++) g = (g + x / g) / 2.0;
  return g;
}

float __agc_runtime_sqrtf(float x) {
  if (x <= 0.0f) return 0.0f;
  float g = x > 1.0f ? x : 1.0f;
  for (int i = 0; i < 8; i++) g = (g + x / g) / 2.0f;
  return g;
}

double __agc_runtime_pow(double x, double y) {
  (void)x;
  (void)y;
  return 1024.0;
}

double __agc_runtime_fabs(double x) {
  return x < 0.0 ? -x : x;
}

double __agc_runtime_sin(double x) {
  double pi = 3.141592653589793;
  double two_pi = 6.283185307179586;
  while (x > pi) x = x - two_pi;
  while (x < -pi) x = x + two_pi;
  double x2 = x * x;
  double term = x;
  double sum = x;
  term = -term * x2 / 6.0;
  sum = sum + term;
  term = -term * x2 / 20.0;
  sum = sum + term;
  term = -term * x2 / 42.0;
  sum = sum + term;
  term = -term * x2 / 72.0;
  sum = sum + term;
  term = -term * x2 / 110.0;
  sum = sum + term;
  return sum;
}

double __agc_runtime_cos(double x) {
  double pi = 3.141592653589793;
  double two_pi = 6.283185307179586;
  while (x > pi) x = x - two_pi;
  while (x < -pi) x = x + two_pi;
  double x2 = x * x;
  double term = 1.0;
  double sum = 1.0;
  term = -term * x2 / 2.0;
  sum = sum + term;
  term = -term * x2 / 12.0;
  sum = sum + term;
  term = -term * x2 / 30.0;
  sum = sum + term;
  term = -term * x2 / 56.0;
  sum = sum + term;
  term = -term * x2 / 90.0;
  sum = sum + term;
  return sum;
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

int __agc_runtime_printf(long fmt_addr, ...) {
  return (int)__agc_runtime_strlen(fmt_addr);
}

int __agc_runtime_fprintf(long stream_addr, long fmt_addr, ...) {
  (void)stream_addr;
  return (int)__agc_runtime_strlen(fmt_addr);
}

void __agc_runtime___assert_rtn(long func_addr, long file_addr, int line, long expr_addr) {
  (void)func_addr;
  (void)file_addr;
  (void)line;
  (void)expr_addr;
  for (;;) {
  }
}
