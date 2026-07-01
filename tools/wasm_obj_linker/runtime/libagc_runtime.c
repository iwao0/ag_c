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
static char ag_rt_strerror[] = "error";
static char ag_rt_file_buf[512];
static long ag_rt_file_len = 0;
static char *ag_rt_strtok_next;
static unsigned long ag_rt_rand_state = 1;
static int ag_rt_round_mode = 0;
static int ag_rt_errno_value = 0;
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

double __agc_runtime_exp(double x);
double __agc_runtime_log(double x);
long __agc_runtime_memcpy(long dst_addr, long src_addr, long n);
long __agc_runtime_wcstol(long nptr_addr, long endptr_addr, int base);
int __agc_runtime_strcmp(long a_addr, long b_addr);

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

long __agc_runtime_realloc(long ptr, long size) {
  if (!ptr) return __agc_runtime_malloc(size);
  if (size == 0) return 0;
  long p = __agc_runtime_malloc(size);
  __agc_runtime_memcpy(p, ptr, size);
  return p;
}

void __agc_runtime_qsort(long base_addr, long nmemb, long size, long compar_addr) {
  if (!base_addr || nmemb <= 1 || size <= 0 || !compar_addr) return;
  int (*cmp)(long, long) = (int (*)(long, long))compar_addr;
  long tmp_addr = __agc_runtime_malloc(size);
  long i = 0;
  while (i < nmemb) {
    long j = i + 1;
    while (j < nmemb) {
      long a = base_addr + i * size;
      long b = base_addr + j * size;
      if (cmp(a, b) > 0) {
        __agc_runtime_memcpy(tmp_addr, a, size);
        __agc_runtime_memcpy(a, b, size);
        __agc_runtime_memcpy(b, tmp_addr, size);
      }
      j++;
    }
    i++;
  }
}

long __agc_runtime_bsearch(long key_addr, long base_addr, long nmemb, long size, long compar_addr) {
  if (!key_addr || !base_addr || nmemb <= 0 || size <= 0 || !compar_addr) return 0;
  int (*cmp)(long, long) = (int (*)(long, long))compar_addr;
  long i = 0;
  while (i < nmemb) {
    long elem = base_addr + i * size;
    int r = cmp(key_addr, elem);
    if (r == 0) return elem;
    i++;
  }
  return 0;
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

long __agc_runtime_memmove(long dst_addr, long src_addr, long n) {
  unsigned char *dst = (unsigned char *)ag_rt_ptr(dst_addr);
  unsigned char *src = (unsigned char *)ag_rt_ptr(src_addr);
  if (dst < src) {
    long i = 0;
    while (i < n) {
      dst[i] = src[i];
      i++;
    }
  } else if (dst > src) {
    long i = n;
    while (i > 0) {
      i--;
      dst[i] = src[i];
    }
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

int __agc_runtime_islower(int c) {
  return c >= 'a' && c <= 'z';
}

int __agc_runtime_isupper(int c) {
  return c >= 'A' && c <= 'Z';
}

int __agc_runtime_isalpha(int c) {
  return __agc_runtime_islower(c) || __agc_runtime_isupper(c);
}

int __agc_runtime_isalnum(int c) {
  return __agc_runtime_isalpha(c) || __agc_runtime_isdigit(c);
}

int __agc_runtime_isblank(int c) {
  return c == ' ' || c == '\t';
}

int __agc_runtime_iscntrl(int c) {
  return (c >= 0 && c < 32) || c == 127;
}

int __agc_runtime_isgraph(int c) {
  return c >= 33 && c <= 126;
}

int __agc_runtime_isprint(int c) {
  return c >= 32 && c <= 126;
}

int __agc_runtime_ispunct(int c) {
  return __agc_runtime_isgraph(c) && !__agc_runtime_isalnum(c);
}

int __agc_runtime_isspace(int c) {
  return c == ' ' || c == '\f' || c == '\n' || c == '\r' || c == '\t' || c == '\v';
}

int __agc_runtime_isxdigit(int c) {
  return __agc_runtime_isdigit(c) ||
         (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

int __agc_runtime_tolower(int c) {
  return __agc_runtime_isupper(c) ? c + 32 : c;
}

int __agc_runtime_toupper(int c) {
  return __agc_runtime_islower(c) ? c - 32 : c;
}

int __agc_runtime_wctype(long property_addr) {
  char *p = ag_rt_ptr(property_addr);
  if (__agc_runtime_strcmp((long)p, (long)"alnum") == 0) return 1;
  if (__agc_runtime_strcmp((long)p, (long)"alpha") == 0) return 2;
  if (__agc_runtime_strcmp((long)p, (long)"blank") == 0) return 3;
  if (__agc_runtime_strcmp((long)p, (long)"digit") == 0) return 4;
  if (__agc_runtime_strcmp((long)p, (long)"space") == 0) return 5;
  if (__agc_runtime_strcmp((long)p, (long)"xdigit") == 0) return 6;
  return 0;
}

int __agc_runtime_iswctype(int wc, int desc) {
  if (desc == 1) return __agc_runtime_isalnum(wc);
  if (desc == 2) return __agc_runtime_isalpha(wc);
  if (desc == 3) return __agc_runtime_isblank(wc);
  if (desc == 4) return __agc_runtime_isdigit(wc);
  if (desc == 5) return __agc_runtime_isspace(wc);
  if (desc == 6) return __agc_runtime_isxdigit(wc);
  return 0;
}

int __agc_runtime_wctrans(long property_addr) {
  char *p = ag_rt_ptr(property_addr);
  if (__agc_runtime_strcmp((long)p, (long)"tolower") == 0) return 1;
  if (__agc_runtime_strcmp((long)p, (long)"toupper") == 0) return 2;
  return 0;
}

int __agc_runtime_towctrans(int wc, int desc) {
  if (desc == 1) return __agc_runtime_tolower(wc);
  if (desc == 2) return __agc_runtime_toupper(wc);
  return wc;
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

long __agc_runtime_wcsncpy(long dst_addr, long src_addr, long n) {
  int *dst = (int *)ag_rt_ptr(dst_addr);
  int *src = (int *)ag_rt_ptr(src_addr);
  long i = 0;
  int ended = 0;
  while (i < n) {
    int c = ended ? 0 : src[i];
    dst[i] = c;
    if (c == 0) ended = 1;
    i++;
  }
  return dst_addr;
}

long __agc_runtime_wcscat(long dst_addr, long src_addr) {
  int *dst = (int *)ag_rt_ptr(dst_addr);
  int *src = (int *)ag_rt_ptr(src_addr);
  long end = 0;
  long i = 0;
  while (dst[end]) end++;
  do {
    dst[end + i] = src[i];
  } while (src[i++] != 0);
  return dst_addr;
}

long __agc_runtime_wcsncat(long dst_addr, long src_addr, long n) {
  int *dst = (int *)ag_rt_ptr(dst_addr);
  int *src = (int *)ag_rt_ptr(src_addr);
  long end = 0;
  long i = 0;
  while (dst[end]) end++;
  while (i < n && src[i]) {
    dst[end + i] = src[i];
    i++;
  }
  dst[end + i] = 0;
  return dst_addr;
}

int __agc_runtime_wcscmp(long a_addr, long b_addr) {
  int *a = (int *)ag_rt_ptr(a_addr);
  int *b = (int *)ag_rt_ptr(b_addr);
  long i = 0;
  while (a[i] && a[i] == b[i]) i++;
  return a[i] - b[i];
}

int __agc_runtime_wcsncmp(long a_addr, long b_addr, long n) {
  int *a = (int *)ag_rt_ptr(a_addr);
  int *b = (int *)ag_rt_ptr(b_addr);
  long i = 0;
  while (i < n) {
    if (a[i] != b[i]) return a[i] - b[i];
    if (a[i] == 0) return 0;
    i++;
  }
  return 0;
}

long __agc_runtime_wcschr(long s_addr, int ch) {
  int *s = (int *)ag_rt_ptr(s_addr);
  long i = 0;
  for (;;) {
    if (s[i] == ch) return s_addr + i * 4;
    if (s[i] == 0) return 0;
    i++;
  }
  return 0;
}

long __agc_runtime_wcsrchr(long s_addr, int ch) {
  int *s = (int *)ag_rt_ptr(s_addr);
  long found = 0;
  long i = 0;
  for (;;) {
    if (s[i] == ch) found = s_addr + i * 4;
    if (s[i] == 0) return found;
    i++;
  }
  return 0;
}

long __agc_runtime_wcsstr(long haystack_addr, long needle_addr) {
  int *haystack = (int *)ag_rt_ptr(haystack_addr);
  int *needle = (int *)ag_rt_ptr(needle_addr);
  if (!needle[0]) return haystack_addr;
  long i = 0;
  while (haystack[i]) {
    long j = 0;
    while (needle[j] && haystack[i + j] == needle[j]) j++;
    if (!needle[j]) return haystack_addr + i * 4;
    i++;
  }
  return 0;
}

long __agc_runtime_wmemcpy(long dst_addr, long src_addr, long n) {
  int *dst = (int *)ag_rt_ptr(dst_addr);
  int *src = (int *)ag_rt_ptr(src_addr);
  long i = 0;
  while (i < n) {
    dst[i] = src[i];
    i++;
  }
  return dst_addr;
}

long __agc_runtime_wmemmove(long dst_addr, long src_addr, long n) {
  int *dst = (int *)ag_rt_ptr(dst_addr);
  int *src = (int *)ag_rt_ptr(src_addr);
  if (dst < src) {
    long i = 0;
    while (i < n) {
      dst[i] = src[i];
      i++;
    }
  } else if (dst > src) {
    long i = n;
    while (i > 0) {
      i--;
      dst[i] = src[i];
    }
  }
  return dst_addr;
}

long __agc_runtime_wmemset(long s_addr, int ch, long n) {
  int *s = (int *)ag_rt_ptr(s_addr);
  long i = 0;
  while (i < n) {
    s[i] = ch;
    i++;
  }
  return s_addr;
}

int __agc_runtime_wmemcmp(long a_addr, long b_addr, long n) {
  int *a = (int *)ag_rt_ptr(a_addr);
  int *b = (int *)ag_rt_ptr(b_addr);
  long i = 0;
  while (i < n) {
    if (a[i] != b[i]) return a[i] - b[i];
    i++;
  }
  return 0;
}

long __agc_runtime_wmemchr(long s_addr, int ch, long n) {
  int *s = (int *)ag_rt_ptr(s_addr);
  long i = 0;
  while (i < n) {
    if (s[i] == ch) return s_addr + i * 4;
    i++;
  }
  return 0;
}

long __agc_runtime_wcstol(long nptr_addr, long endptr_addr, int base) {
  int *s = (int *)ag_rt_ptr(nptr_addr);
  while (*s == ' ' || *s == '\f' || *s == '\n' || *s == '\r' || *s == '\t' || *s == '\v') s++;
  int sign = 1;
  if (*s == '-') {
    sign = -1;
    s++;
  } else if (*s == '+') {
    s++;
  }
  if (base == 0) base = 10;
  if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
  long acc = 0;
  for (;;) {
    int digit;
    if (*s >= '0' && *s <= '9') digit = *s - '0';
    else if (*s >= 'a' && *s <= 'z') digit = *s - 'a' + 10;
    else if (*s >= 'A' && *s <= 'Z') digit = *s - 'A' + 10;
    else break;
    if (digit >= base) break;
    acc = acc * base + digit;
    s++;
  }
  if (endptr_addr) {
    long *endp = (long *)ag_rt_ptr(endptr_addr);
    *endp = (long)s;
  }
  return sign * acc;
}

unsigned long __agc_runtime_wcstoul(long nptr_addr, long endptr_addr, int base) {
  return (unsigned long)__agc_runtime_wcstol(nptr_addr, endptr_addr, base);
}

double __agc_runtime_wcstod(long nptr_addr, long endptr_addr) {
  int *s = (int *)ag_rt_ptr(nptr_addr);
  while (*s == ' ' || *s == '\f' || *s == '\n' || *s == '\r' || *s == '\t' || *s == '\v') s++;
  double sign = 1.0;
  if (*s == '-') {
    sign = -1.0;
    s++;
  } else if (*s == '+') {
    s++;
  }
  double acc = 0.0;
  while (*s >= '0' && *s <= '9') {
    acc = acc * 10.0 + (double)(*s - '0');
    s++;
  }
  if (*s == '.') {
    double place = 0.1;
    s++;
    while (*s >= '0' && *s <= '9') {
      acc = acc + (double)(*s - '0') * place;
      place = place / 10.0;
      s++;
    }
  }
  if (endptr_addr) {
    long *endp = (long *)ag_rt_ptr(endptr_addr);
    *endp = (long)s;
  }
  return sign * acc;
}

long __agc_runtime_mbrtowc(long pwc_addr, long s_addr, long n, long ps_addr) {
  (void)ps_addr;
  if (!s_addr) return 0;
  if (n == 0) return -2;
  char *s = ag_rt_ptr(s_addr);
  int *pwc = (int *)ag_rt_ptr(pwc_addr);
  if (pwc) *pwc = (unsigned char)s[0];
  return s[0] == 0 ? 0 : 1;
}

long __agc_runtime_wcrtomb(long s_addr, int wc, long ps_addr) {
  (void)ps_addr;
  if (!s_addr) return 1;
  char *s = ag_rt_ptr(s_addr);
  s[0] = (char)wc;
  return 1;
}

long __agc_runtime_mbsrtowcs(long dst_addr, long srcp_addr, long len, long ps_addr) {
  (void)ps_addr;
  long *srcp = (long *)ag_rt_ptr(srcp_addr);
  char *src = ag_rt_ptr(*srcp);
  int *dst = dst_addr ? (int *)ag_rt_ptr(dst_addr) : (int *)0;
  long i = 0;
  while (i < len && src[i]) {
    if (dst) dst[i] = (unsigned char)src[i];
    i++;
  }
  if (i < len && src[i] == 0) {
    if (dst) dst[i] = 0;
    *srcp = 0;
  }
  return i;
}

long __agc_runtime_wcsrtombs(long dst_addr, long srcp_addr, long len, long ps_addr) {
  (void)ps_addr;
  long *srcp = (long *)ag_rt_ptr(srcp_addr);
  int *src = (int *)ag_rt_ptr(*srcp);
  char *dst = dst_addr ? ag_rt_ptr(dst_addr) : (char *)0;
  long i = 0;
  while (i < len && src[i]) {
    if (dst) dst[i] = (char)src[i];
    i++;
  }
  if (i < len && src[i] == 0) {
    if (dst) dst[i] = 0;
    *srcp = 0;
  }
  return i;
}

int __agc_runtime_btowc(int c) {
  return c == -1 ? -1 : (c & 255);
}

int __agc_runtime_wctob(int c) {
  return (c >= 0 && c <= 255) ? c : -1;
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

long __agc_runtime_strtol(long s_addr, long endptr_addr, int base) {
  char *s = ag_rt_ptr(s_addr);
  while (*s == ' ' || *s == '\f' || *s == '\n' || *s == '\r' || *s == '\t' || *s == '\v') s++;
  int sign = 1;
  if (*s == '-') {
    sign = -1;
    s++;
  } else if (*s == '+') {
    s++;
  }
  if (base == 0) base = 10;
  if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
  long acc = 0;
  for (;;) {
    int digit;
    if (*s >= '0' && *s <= '9') digit = *s - '0';
    else if (*s >= 'a' && *s <= 'z') digit = *s - 'a' + 10;
    else if (*s >= 'A' && *s <= 'Z') digit = *s - 'A' + 10;
    else break;
    if (digit >= base) break;
    acc = acc * base + digit;
    s++;
  }
  if (endptr_addr) {
    long *endp = (long *)ag_rt_ptr(endptr_addr);
    *endp = (long)s;
  }
  return sign * acc;
}

long __agc_runtime_atol(long s_addr) {
  return __agc_runtime_strtol(s_addr, 0, 10);
}

long __agc_runtime_labs(long x) {
  return x < 0 ? -x : x;
}

void __agc_runtime_srand(int seed) {
  ag_rt_rand_state = (unsigned long)seed;
}

int __agc_runtime_rand(void) {
  ag_rt_rand_state = ag_rt_rand_state * 1103515245u + 12345u;
  return (int)((ag_rt_rand_state / 65536u) & 32767u);
}

int __agc_runtime_atexit(long func_addr) {
  (void)func_addr;
  return 0;
}

void __agc_runtime_exit(int status) {
  (void)status;
  for (;;) {
  }
}

void __agc_runtime_abort(void) {
  for (;;) {
  }
}

long __agc_runtime_getenv(long name_addr) {
  (void)name_addr;
  return 0;
}

int __agc_runtime_system(long command_addr) {
  (void)command_addr;
  return 0;
}

long __agc_runtime_signal(int sig, long handler_addr) {
  (void)sig;
  (void)handler_addr;
  return 0;
}

int __agc_runtime_raise(int sig) {
  (void)sig;
  return 0;
}

long __agc_runtime_time(long tloc_addr) {
  if (tloc_addr) {
    long *tloc = (long *)ag_rt_ptr(tloc_addr);
    *tloc = 0;
  }
  return 0;
}

long __agc_runtime_clock(void) {
  return 0;
}

double __agc_runtime_difftime(long end, long beginning) {
  return (double)(end - beginning);
}

long __agc_runtime___error(void) {
  return (long)&ag_rt_errno_value;
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

long __agc_runtime_strncat(long dst_addr, long src_addr, long n) {
  char *dst = ag_rt_ptr(dst_addr);
  char *src = ag_rt_ptr(src_addr);
  long end = 0;
  long i = 0;
  while (dst[end]) end++;
  while (i < n && src[i]) {
    dst[end + i] = src[i];
    i++;
  }
  dst[end + i] = 0;
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

long __agc_runtime_memchr(long s_addr, int ch, long n) {
  unsigned char *s = (unsigned char *)ag_rt_ptr(s_addr);
  int needle = ch & 255;
  long i = 0;
  while (i < n) {
    if ((int)s[i] == needle) return s_addr + i;
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

long __agc_runtime_strstr(long haystack_addr, long needle_addr) {
  char *haystack = ag_rt_ptr(haystack_addr);
  char *needle = ag_rt_ptr(needle_addr);
  if (!needle[0]) return haystack_addr;
  long i = 0;
  while (haystack[i]) {
    long j = 0;
    while (needle[j] && haystack[i + j] == needle[j]) j++;
    if (!needle[j]) return haystack_addr + i;
    i++;
  }
  return 0;
}

static int ag_rt_strtok_is_delim(char ch, char *delim) {
  long i = 0;
  while (delim[i]) {
    if (ch == delim[i]) return 1;
    i++;
  }
  return 0;
}

long __agc_runtime_strtok(long str_addr, long delim_addr) {
  char *s = str_addr ? ag_rt_ptr(str_addr) : ag_rt_strtok_next;
  char *delim = ag_rt_ptr(delim_addr);
  if (!s) return 0;
  while (*s && ag_rt_strtok_is_delim(*s, delim)) s++;
  if (!*s) {
    ag_rt_strtok_next = 0;
    return 0;
  }
  char *start = s;
  while (*s && !ag_rt_strtok_is_delim(*s, delim)) s++;
  if (*s) {
    *s = 0;
    ag_rt_strtok_next = s + 1;
  } else {
    ag_rt_strtok_next = 0;
  }
  return (long)start;
}

long __agc_runtime_strerror(int errnum) {
  (void)errnum;
  return (long)ag_rt_strerror;
}

int __agc_runtime_putchar(int c) {
  return c;
}

int __agc_runtime_puts(long s_addr) {
  return (int)__agc_runtime_strlen(s_addr) + 1;
}

int __agc_runtime_fputs(long s_addr, long stream_addr) {
  (void)stream_addr;
  return (int)__agc_runtime_strlen(s_addr);
}

int __agc_runtime_fputc(int c, long stream_addr) {
  (void)stream_addr;
  return c;
}

int __agc_runtime_fflush(long stream_addr) {
  (void)stream_addr;
  return 0;
}

void __agc_runtime_perror(long s_addr) {
  (void)s_addr;
}

int __agc_runtime_getchar(void) {
  return -1;
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

int __agc_runtime_fegetexceptflag(long flagp_addr, int excepts) {
  unsigned long long *flagp = (unsigned long long *)ag_rt_ptr(flagp_addr);
  if (flagp) *flagp = (unsigned long long)excepts;
  return 0;
}

int __agc_runtime_feraiseexcept(int excepts) {
  (void)excepts;
  return 0;
}

int __agc_runtime_fesetexceptflag(long flagp_addr, int excepts) {
  (void)flagp_addr;
  (void)excepts;
  return 0;
}

int __agc_runtime_fetestexcept(int excepts) {
  return excepts;
}

int __agc_runtime_fegetround(void) {
  return ag_rt_round_mode;
}

int __agc_runtime_fesetround(int round) {
  ag_rt_round_mode = round;
  return 0;
}

int __agc_runtime_fegetenv(long envp_addr) {
  unsigned long long *envp = (unsigned long long *)ag_rt_ptr(envp_addr);
  if (envp) {
    envp[0] = (unsigned long long)ag_rt_round_mode;
    envp[1] = 0;
  }
  return 0;
}

int __agc_runtime_feholdexcept(long envp_addr) {
  return __agc_runtime_fegetenv(envp_addr);
}

int __agc_runtime_fesetenv(long envp_addr) {
  unsigned long long *envp = (unsigned long long *)ag_rt_ptr(envp_addr);
  if (envp) ag_rt_round_mode = (int)envp[0];
  return 0;
}

int __agc_runtime_feupdateenv(long envp_addr) {
  return __agc_runtime_fesetenv(envp_addr);
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

long double __agc_runtime_sqrtl(long double x) {
  return (long double)__agc_runtime_sqrt((double)x);
}

double __agc_runtime_pow(double x, double y) {
  long yi = (long)y;
  if ((double)yi == y) {
    double base = x;
    double result = 1.0;
    long n = yi < 0 ? -yi : yi;
    while (n > 0) {
      if (n & 1) result = result * base;
      base = base * base;
      n = n / 2;
    }
    return yi < 0 ? 1.0 / result : result;
  }
  if (x <= 0.0) return 0.0;
  return __agc_runtime_exp(y * __agc_runtime_log(x));
}

float __agc_runtime_powf(float x, float y) {
  return (float)__agc_runtime_pow((double)x, (double)y);
}

long double __agc_runtime_powl(long double x, long double y) {
  return (long double)__agc_runtime_pow((double)x, (double)y);
}

double __agc_runtime_fabs(double x) {
  return x < 0.0 ? -x : x;
}

float __agc_runtime_fabsf(float x) {
  return x < 0.0f ? -x : x;
}

long double __agc_runtime_fabsl(long double x) {
  return x < 0.0L ? -x : x;
}

double __agc_runtime_trunc(double x) {
  return (double)(long)x;
}

double __agc_runtime_floor(double x) {
  long i = (long)x;
  if ((double)i > x) i = i - 1;
  return (double)i;
}

double __agc_runtime_ceil(double x) {
  long i = (long)x;
  if ((double)i < x) i = i + 1;
  return (double)i;
}

double __agc_runtime_round(double x) {
  return x < 0.0 ? __agc_runtime_ceil(x - 0.5) : __agc_runtime_floor(x + 0.5);
}

float __agc_runtime_floorf(float x) {
  return (float)__agc_runtime_floor((double)x);
}

float __agc_runtime_ceilf(float x) {
  return (float)__agc_runtime_ceil((double)x);
}

float __agc_runtime_roundf(float x) {
  return (float)__agc_runtime_round((double)x);
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

double __agc_runtime_tan(double x) {
  double c = __agc_runtime_cos(x);
  if (c == 0.0) return x < 0.0 ? -1.0e308 : 1.0e308;
  return __agc_runtime_sin(x) / c;
}

double __agc_runtime_fmod(double x, double y) {
  if (y == 0.0) return 0.0;
  long q = (long)(x / y);
  double r = x - (double)q * y;
  if (x >= 0.0 && r < 0.0) r = r + __agc_runtime_fabs(y);
  if (x < 0.0 && r > 0.0) r = r - __agc_runtime_fabs(y);
  return r;
}

float __agc_runtime_fmodf(float x, float y) {
  return (float)__agc_runtime_fmod((double)x, (double)y);
}

long double __agc_runtime_fmodl(long double x, long double y) {
  return (long double)__agc_runtime_fmod((double)x, (double)y);
}

double __agc_runtime_cbrt(double x) {
  if (x == 0.0) return 0.0;
  double sign = x < 0.0 ? -1.0 : 1.0;
  double a = x < 0.0 ? -x : x;
  double g = a > 1.0 ? a : 1.0;
  for (int i = 0; i < 24; i++) g = (2.0 * g + a / (g * g)) / 3.0;
  return sign * g;
}

double __agc_runtime_exp(double x) {
  double ln2 = 0.6931471805599453;
  int k = 0;
  while (x > ln2) {
    x = x - ln2;
    k++;
  }
  while (x < -ln2) {
    x = x + ln2;
    k--;
  }
  double term = 1.0;
  double sum = 1.0;
  for (int i = 1; i <= 18; i++) {
    term = term * x / (double)i;
    sum = sum + term;
  }
  while (k > 0) {
    sum = sum * 2.0;
    k--;
  }
  while (k < 0) {
    sum = sum / 2.0;
    k++;
  }
  return sum;
}

double __agc_runtime_log(double x) {
  if (x <= 0.0) return 0.0;
  int k = 0;
  while (x > 2.0) {
    x = x / 2.0;
    k++;
  }
  while (x < 0.5) {
    x = x * 2.0;
    k--;
  }
  double y = (x - 1.0) / (x + 1.0);
  double y2 = y * y;
  double term = y;
  double sum = 0.0;
  for (int n = 1; n <= 39; n = n + 2) {
    sum = sum + term / (double)n;
    term = term * y2;
  }
  return 2.0 * sum + (double)k * 0.6931471805599453;
}

double __agc_runtime_log2(double x) {
  return __agc_runtime_log(x) / 0.6931471805599453;
}

double __agc_runtime_log10(double x) {
  return __agc_runtime_log(x) / 2.302585092994046;
}

static double ag_rt_atan_core(double x) {
  double x2 = x * x;
  double term = x;
  double sum = x;
  double den = 3.0;
  int neg = 1;
  for (int i = 0; i < 96; i = i + 1) {
    term = term * x2;
    if (neg) sum = sum - term / den;
    else sum = sum + term / den;
    neg = !neg;
    den = den + 2.0;
  }
  return sum;
}

double __agc_runtime_atan(double x) {
  if (x > 1.0) return 1.5707963267948966 - ag_rt_atan_core(1.0 / x);
  if (x < -1.0) return -1.5707963267948966 - ag_rt_atan_core(1.0 / x);
  return ag_rt_atan_core(x);
}

double __agc_runtime_atan2(double y, double x) {
  if (x > 0.0) return __agc_runtime_atan(y / x);
  if (x < 0.0) {
    if (y >= 0.0) return __agc_runtime_atan(y / x) + 3.141592653589793;
    return __agc_runtime_atan(y / x) - 3.141592653589793;
  }
  if (y > 0.0) return 1.5707963267948966;
  if (y < 0.0) return -1.5707963267948966;
  return 0.0;
}

double __agc_runtime_asin(double x) {
  return __agc_runtime_atan2(x, __agc_runtime_sqrt(1.0 - x * x));
}

double __agc_runtime_acos(double x) {
  return __agc_runtime_atan2(__agc_runtime_sqrt(1.0 - x * x), x);
}

double __agc_runtime_sinh(double x) {
  double ex = __agc_runtime_exp(x);
  double em = __agc_runtime_exp(-x);
  return (ex - em) / 2.0;
}

double __agc_runtime_cosh(double x) {
  double ex = __agc_runtime_exp(x);
  double em = __agc_runtime_exp(-x);
  return (ex + em) / 2.0;
}

double __agc_runtime_tanh(double x) {
  double ex = __agc_runtime_exp(x);
  double em = __agc_runtime_exp(-x);
  return (ex - em) / (ex + em);
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
  char *fmt = (char *)(long)fmt_addr;
  va_list ap;
  va_start(ap, fmt_addr);
  int n = ag_rt_vformat((char *)0, 0, 1, fmt, ap);
  va_end(ap);
  return n;
}

int __agc_runtime_fprintf(long stream_addr, long fmt_addr, ...) {
  (void)stream_addr;
  char *fmt = (char *)(long)fmt_addr;
  va_list ap;
  va_start(ap, fmt_addr);
  int n = ag_rt_vformat((char *)0, 0, 1, fmt, ap);
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
