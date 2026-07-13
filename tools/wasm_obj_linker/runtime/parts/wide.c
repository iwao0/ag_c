int __agc_runtime_fgetc(long stream_addr);
int __agc_runtime_getchar(void);
int __agc_runtime_ungetc(int c, long stream_addr);
int __agc_runtime_fputc(int c, long stream_addr);
int __agc_runtime_putchar(int c);

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

int __agc_runtime_wcscoll(long a_addr, long b_addr) {
  return __agc_runtime_wcscmp(a_addr, b_addr);
}

unsigned long __agc_runtime_wcsxfrm(long dst_addr, long src_addr, unsigned long n) {
  int *dst = dst_addr ? (int *)ag_rt_ptr(dst_addr) : (int *)0;
  int *src = (int *)ag_rt_ptr(src_addr);
  long limit = (long)n;
  long len = 0;
  while (src[len]) len++;
  if (dst && limit != 0) {
    long i = 0;
    while (i + 1 < limit && src[i]) {
      dst[i] = src[i];
      i++;
    }
    dst[i] = 0;
  }
  return (unsigned long)len;
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

static int ag_rt_wcs_contains(int *set, int ch) {
  long i = 0;
  while (set[i]) {
    if (set[i] == ch) return 1;
    i++;
  }
  return 0;
}

unsigned long __agc_runtime_wcsspn(long s_addr, long accept_addr) {
  int *s = (int *)ag_rt_ptr(s_addr);
  int *accept = (int *)ag_rt_ptr(accept_addr);
  unsigned long i = 0;
  while (s[i] && ag_rt_wcs_contains(accept, s[i])) i++;
  return i;
}

unsigned long __agc_runtime_wcscspn(long s_addr, long reject_addr) {
  int *s = (int *)ag_rt_ptr(s_addr);
  int *reject = (int *)ag_rt_ptr(reject_addr);
  unsigned long i = 0;
  while (s[i] && !ag_rt_wcs_contains(reject, s[i])) i++;
  return i;
}

long __agc_runtime_wcspbrk(long s_addr, long accept_addr) {
  int *s = (int *)ag_rt_ptr(s_addr);
  int *accept = (int *)ag_rt_ptr(accept_addr);
  long i = 0;
  while (s[i]) {
    if (ag_rt_wcs_contains(accept, s[i])) return s_addr + i * 4;
    i++;
  }
  return 0;
}

long __agc_runtime_wcstok(long s_addr, long delim_addr, long saveptr_addr) {
  int *delim = (int *)ag_rt_ptr(delim_addr);
  long *saveptr = (long *)ag_rt_ptr(saveptr_addr);
  int *s = s_addr ? (int *)ag_rt_ptr(s_addr) : (int *)ag_rt_ptr(*saveptr);
  int *tok;
  if (!s) return 0;
  while (*s && ag_rt_wcs_contains(delim, *s)) s++;
  if (!*s) {
    *saveptr = 0;
    return 0;
  }
  tok = s;
  while (*s && !ag_rt_wcs_contains(delim, *s)) s++;
  if (*s) {
    *s = 0;
    s++;
    *saveptr = (long)s;
  } else {
    *saveptr = 0;
  }
  return (long)tok;
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

static char ag_rt_wide_strto_tmp[512];

static long ag_rt_wide_copy_ascii(int *src) {
  long n = 0;
  while (src[n] && n + 1 < (long)sizeof(ag_rt_wide_strto_tmp)) {
    int ch = src[n];
    ag_rt_wide_strto_tmp[n] = (ch > 0 && ch < 128) ? (char)ch : 0;
    if (ag_rt_wide_strto_tmp[n] == 0) break;
    n++;
  }
  ag_rt_wide_strto_tmp[n] = 0;
  return n;
}

static void ag_rt_wide_store_end(long endptr_addr, int *end) {
  if (endptr_addr) {
    long *endp = (long *)ag_rt_ptr(endptr_addr);
    *endp = (long)end;
  }
}

static int ag_rt_wide_digit_value(int ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'z') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'Z') return ch - 'A' + 10;
  return -1;
}

static int ag_rt_wide_ascii_lower(int ch) {
  if (ch >= 'A' && ch <= 'Z') return ch - 'A' + 'a';
  return ch;
}

static int ag_rt_wide_match_lit(int *p, char *lit) {
  int n = 0;
  while (lit[n]) {
    if (ag_rt_wide_ascii_lower(p[n]) != lit[n]) return 0;
    n++;
  }
  return n;
}

static int ag_rt_wide_nan_payload_len(int *p) {
  int n = 0;
  if (*p != '(') return 0;
  p++;
  n++;
  while (*p && *p != ')') {
    p++;
    n++;
  }
  if (*p == ')') return n + 1;
  return 0;
}

static int *ag_rt_wide_int_end(int *orig, int base) {
  int *s = orig;
  int *digits;
  int digit;
  if (!(base == 0 || (base >= 2 && base <= 36))) return orig;
  while (*s == ' ' || *s == '\f' || *s == '\n' || *s == '\r' || *s == '\t' || *s == '\v') s++;
  if (*s == '-' || *s == '+') s++;
  if (base == 0) {
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X') &&
        ag_rt_wide_digit_value(s[2]) >= 0 && ag_rt_wide_digit_value(s[2]) < 16) {
      base = 16;
      s += 2;
    } else if (s[0] == '0') {
      base = 8;
    } else {
      base = 10;
    }
  } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X') &&
             ag_rt_wide_digit_value(s[2]) >= 0 && ag_rt_wide_digit_value(s[2]) < 16) {
    s += 2;
  }
  digits = s;
  for (;;) {
    digit = ag_rt_wide_digit_value(*s);
    if (digit < 0 || digit >= base) break;
    s++;
  }
  return s == digits ? orig : s;
}

static int *ag_rt_wide_float_end(int *orig) {
  int *s = orig;
  int *digits;
  int have_digit = 0;
  int special_len;
  while (*s == ' ' || *s == '\f' || *s == '\n' || *s == '\r' || *s == '\t' || *s == '\v') s++;
  if (*s == '-' || *s == '+') s++;
  special_len = ag_rt_wide_match_lit(s, "infinity");
  if (special_len == 0) special_len = ag_rt_wide_match_lit(s, "inf");
  if (special_len != 0) return s + special_len;
  special_len = ag_rt_wide_match_lit(s, "nan");
  if (special_len != 0) return s + special_len + ag_rt_wide_nan_payload_len(s + special_len);
  if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    int *p = s + 2;
    while (ag_rt_wide_digit_value(*p) >= 0 && ag_rt_wide_digit_value(*p) < 16) {
      have_digit = 1;
      p++;
    }
    if (ag_rt_is_decimal_point(*p)) {
      p++;
      while (ag_rt_wide_digit_value(*p) >= 0 && ag_rt_wide_digit_value(*p) < 16) {
        have_digit = 1;
        p++;
      }
    }
    if (!have_digit || !(*p == 'p' || *p == 'P')) return orig;
    p++;
    if (*p == '-' || *p == '+') p++;
    digits = p;
    while (*p >= '0' && *p <= '9') p++;
    return p == digits ? orig : p;
  }
  digits = s;
  while (*s >= '0' && *s <= '9') {
    have_digit = 1;
    s++;
  }
  if (ag_rt_is_decimal_point(*s)) {
    s++;
    while (*s >= '0' && *s <= '9') {
      have_digit = 1;
      s++;
    }
  }
  if (!have_digit) return orig;
  if (*s == 'e' || *s == 'E') {
    int *exp = s + 1;
    if (*exp == '-' || *exp == '+') exp++;
    digits = exp;
    while (*exp >= '0' && *exp <= '9') exp++;
    if (exp != digits) s = exp;
  }
  return s;
}

long __agc_runtime_wcstol(long nptr_addr, long endptr_addr, int base) {
  int *orig = (int *)ag_rt_ptr(nptr_addr);
  ag_rt_wide_copy_ascii(orig);
  long value = __agc_runtime_strtol((long)ag_rt_wide_strto_tmp, 0, base);
  ag_rt_wide_store_end(endptr_addr, ag_rt_wide_int_end(orig, base));
  return value;
}

unsigned long __agc_runtime_wcstoul(long nptr_addr, long endptr_addr, int base) {
  int *orig = (int *)ag_rt_ptr(nptr_addr);
  ag_rt_wide_copy_ascii(orig);
  unsigned long value = __agc_runtime_strtoul((long)ag_rt_wide_strto_tmp, 0, base);
  ag_rt_wide_store_end(endptr_addr, ag_rt_wide_int_end(orig, base));
  return value;
}

long long __agc_runtime_wcstoll(long nptr_addr, long endptr_addr, int base) {
  int *orig = (int *)ag_rt_ptr(nptr_addr);
  ag_rt_wide_copy_ascii(orig);
  long long value = __agc_runtime_strtoll((long)ag_rt_wide_strto_tmp, 0, base);
  ag_rt_wide_store_end(endptr_addr, ag_rt_wide_int_end(orig, base));
  return value;
}

unsigned long long __agc_runtime_wcstoull(long nptr_addr, long endptr_addr, int base) {
  int *orig = (int *)ag_rt_ptr(nptr_addr);
  ag_rt_wide_copy_ascii(orig);
  unsigned long long value = __agc_runtime_strtoull((long)ag_rt_wide_strto_tmp, 0, base);
  ag_rt_wide_store_end(endptr_addr, ag_rt_wide_int_end(orig, base));
  return value;
}

float __agc_runtime_wcstof(long nptr_addr, long endptr_addr) {
  int *orig = (int *)ag_rt_ptr(nptr_addr);
  ag_rt_wide_copy_ascii(orig);
  float value = __agc_runtime_strtof((long)ag_rt_wide_strto_tmp, 0);
  ag_rt_wide_store_end(endptr_addr, ag_rt_wide_float_end(orig));
  return value;
}

double __agc_runtime_wcstod(long nptr_addr, long endptr_addr) {
  int *orig = (int *)ag_rt_ptr(nptr_addr);
  ag_rt_wide_copy_ascii(orig);
  double value = __agc_runtime_strtod((long)ag_rt_wide_strto_tmp, 0);
  ag_rt_wide_store_end(endptr_addr, ag_rt_wide_float_end(orig));
  return value;
}

double __agc_runtime_wcstold(long nptr_addr, long endptr_addr) {
  int *orig = (int *)ag_rt_ptr(nptr_addr);
  ag_rt_wide_copy_ascii(orig);
  double value = (double)__agc_runtime_strtold((long)ag_rt_wide_strto_tmp, 0);
  ag_rt_wide_store_end(endptr_addr, ag_rt_wide_float_end(orig));
  return value;
}

struct ag_rt_mbstate {
  unsigned int bytes;
  unsigned int have;
  unsigned int need;
  unsigned int pending;
  unsigned int kind;
};

static struct ag_rt_mbstate ag_rt_mbrtowc_state;
static struct ag_rt_mbstate ag_rt_wcrtomb_state;
static struct ag_rt_mbstate ag_rt_mbrlen_state;
static struct ag_rt_mbstate ag_rt_mblen_state;
static struct ag_rt_mbstate ag_rt_mbtowc_state;
static struct ag_rt_mbstate ag_rt_wctomb_state;
static struct ag_rt_mbstate ag_rt_mbrtoc16_state;
static struct ag_rt_mbstate ag_rt_mbrtoc32_state;
static struct ag_rt_mbstate ag_rt_c16rtomb_state;
static struct ag_rt_mbstate ag_rt_mbsrtowcs_state;
static struct ag_rt_mbstate ag_rt_wcsrtombs_state;

static struct ag_rt_mbstate *ag_rt_mbstate_at(long ps_addr,
                                               struct ag_rt_mbstate *fallback) {
  return ps_addr ? (struct ag_rt_mbstate *)ag_rt_ptr(ps_addr) : fallback;
}

static void ag_rt_mbstate_reset(struct ag_rt_mbstate *state) {
  state->bytes = 0;
  state->have = 0;
  state->need = 0;
  state->pending = 0;
  state->kind = 0;
}

static int ag_rt_utf8_start_need(unsigned int byte) {
  if (byte < 0x80) return 1;
  if (byte >= 0xc2 && byte <= 0xdf) return 2;
  if (byte >= 0xe0 && byte <= 0xef) return 3;
  if (byte >= 0xf0 && byte <= 0xf4) return 4;
  return 0;
}

static int ag_rt_utf8_decode_state(struct ag_rt_mbstate *state, unsigned int *out) {
  unsigned int b0 = state->bytes & 0xff;
  unsigned int b1 = (state->bytes >> 8) & 0xff;
  unsigned int b2 = (state->bytes >> 16) & 0xff;
  unsigned int b3 = (state->bytes >> 24) & 0xff;
  unsigned int value = 0;
  if (state->need == 1) value = b0;
  else if (state->need == 2) value = ((b0 & 0x1f) << 6) | (b1 & 0x3f);
  else if (state->need == 3) value = ((b0 & 0x0f) << 12) | ((b1 & 0x3f) << 6) | (b2 & 0x3f);
  else value = ((b0 & 0x07) << 18) | ((b1 & 0x3f) << 12) |
               ((b2 & 0x3f) << 6) | (b3 & 0x3f);
  if ((state->need == 2 && value < 0x80) ||
      (state->need == 3 && value < 0x800) ||
      (state->need == 4 && value < 0x10000) ||
      (value >= 0xd800 && value <= 0xdfff) || value > 0x10ffff) {
    return 0;
  }
  *out = value;
  return 1;
}

static long ag_rt_mbrtowc_stateful(long pwc_addr, long s_addr, long n,
                                   struct ag_rt_mbstate *state) {
  char *s;
  long consumed = 0;
  unsigned int value = 0;
  if (!s_addr) {
    ag_rt_mbstate_reset(state);
    return 0;
  }
  s = ag_rt_ptr(s_addr);
  if (state->kind != 0) {
    ag_rt_mbstate_reset(state);
    ag_rt_set_errno(AG_RT_EILSEQ);
    return -1;
  }
  if (state->have == 0) {
    int need;
    if (n == 0) return -2;
    need = ag_rt_utf8_start_need((unsigned char)s[0]);
    if (!need) {
      ag_rt_set_errno(AG_RT_EILSEQ);
      return -1;
    }
    state->need = (unsigned int)need;
  }
  while (state->have < state->need && consumed < n) {
    unsigned int byte = (unsigned char)s[consumed];
    if (state->have > 0 && (byte & 0xc0) != 0x80) {
      ag_rt_mbstate_reset(state);
      ag_rt_set_errno(AG_RT_EILSEQ);
      return -1;
    }
    state->bytes |= byte << (state->have * 8);
    state->have++;
    consumed++;
  }
  if (state->have < state->need) return -2;
  if (!ag_rt_utf8_decode_state(state, &value)) {
    ag_rt_mbstate_reset(state);
    ag_rt_set_errno(AG_RT_EILSEQ);
    return -1;
  }
  ag_rt_mbstate_reset(state);
  if (pwc_addr) *(int *)ag_rt_ptr(pwc_addr) = (int)value;
  return value == 0 ? 0 : consumed;
}

long __agc_runtime_mbrtowc(long pwc_addr, long s_addr, long n, long ps_addr) {
  return ag_rt_mbrtowc_stateful(pwc_addr, s_addr, n,
                                ag_rt_mbstate_at(ps_addr, &ag_rt_mbrtowc_state));
}

long __agc_runtime_wcrtomb(long s_addr, int wc, long ps_addr) {
  struct ag_rt_mbstate *state = ag_rt_mbstate_at(ps_addr, &ag_rt_wcrtomb_state);
  if (!s_addr) {
    ag_rt_mbstate_reset(state);
    return 1;
  }
  char *s = ag_rt_ptr(s_addr);
  if (wc < 0 || (wc >= 0xd800 && wc <= 0xdfff) || wc > 0x10ffff) {
    ag_rt_set_errno(AG_RT_EILSEQ);
    return -1;
  }
  if (wc <= 0x7f) {
    s[0] = (char)wc;
    return 1;
  }
  if (wc <= 0x7ff) {
    s[0] = (char)(0xc0 | ((wc >> 6) & 0x1f));
    s[1] = (char)(0x80 | (wc & 0x3f));
    return 2;
  }
  if (wc <= 0xffff) {
    s[0] = (char)(0xe0 | ((wc >> 12) & 0x0f));
    s[1] = (char)(0x80 | ((wc >> 6) & 0x3f));
    s[2] = (char)(0x80 | (wc & 0x3f));
    return 3;
  }
  if (wc <= 0x10ffff) {
    s[0] = (char)(0xf0 | ((wc >> 18) & 0x07));
    s[1] = (char)(0x80 | ((wc >> 12) & 0x3f));
    s[2] = (char)(0x80 | ((wc >> 6) & 0x3f));
    s[3] = (char)(0x80 | (wc & 0x3f));
    return 4;
  }
  return -1;
}

long __agc_runtime_mbrtoc16(long pc16_addr, long s_addr, long n, long ps_addr) {
  struct ag_rt_mbstate *state = ag_rt_mbstate_at(ps_addr, &ag_rt_mbrtoc16_state);
  unsigned short *pc16 = (unsigned short *)ag_rt_ptr(pc16_addr);
  if (!s_addr) {
    ag_rt_mbstate_reset(state);
    return 0;
  }
  if (state->kind == 1) {
    if (pc16) *pc16 = (unsigned short)state->pending;
    ag_rt_mbstate_reset(state);
    return -3;
  }
  int wc = 0;
  long r = ag_rt_mbrtowc_stateful((long)&wc, s_addr, n, state);
  if (r < 0) return r;
  if (wc > 0xffff) {
    unsigned int scalar = (unsigned int)wc - 0x10000;
    if (pc16) *pc16 = (unsigned short)(0xd800 + (scalar >> 10));
    state->pending = 0xdc00 + (scalar & 0x3ff);
    state->kind = 1;
  } else if (pc16) {
    *pc16 = (unsigned short)wc;
  }
  return r;
}

long __agc_runtime_c16rtomb(long s_addr, int c16, long ps_addr) {
  struct ag_rt_mbstate *state = ag_rt_mbstate_at(ps_addr, &ag_rt_c16rtomb_state);
  if (!s_addr) {
    ag_rt_mbstate_reset(state);
    return 1;
  }
  if (state->kind == 2) {
    unsigned int scalar;
    if (c16 < 0xdc00 || c16 > 0xdfff) {
      ag_rt_mbstate_reset(state);
      ag_rt_set_errno(AG_RT_EILSEQ);
      return -1;
    }
    scalar = 0x10000 + ((state->pending - 0xd800) << 10) +
             ((unsigned int)c16 - 0xdc00);
    ag_rt_mbstate_reset(state);
    return __agc_runtime_wcrtomb(s_addr, (int)scalar, 0);
  }
  if (c16 >= 0xd800 && c16 <= 0xdbff) {
    state->pending = (unsigned int)c16;
    state->kind = 2;
    return 0;
  }
  if (c16 >= 0xdc00 && c16 <= 0xdfff) {
    ag_rt_set_errno(AG_RT_EILSEQ);
    return -1;
  }
  return __agc_runtime_wcrtomb(s_addr, c16, 0);
}

long __agc_runtime_mbrtoc32(long pc32_addr, long s_addr, long n, long ps_addr) {
  struct ag_rt_mbstate *state = ag_rt_mbstate_at(ps_addr, &ag_rt_mbrtoc32_state);
  if (!s_addr) {
    ag_rt_mbstate_reset(state);
    return 0;
  }
  int wc = 0;
  long r = ag_rt_mbrtowc_stateful((long)&wc, s_addr, n, state);
  if (r < 0) return r;
  unsigned int *pc32 = (unsigned int *)ag_rt_ptr(pc32_addr);
  if (pc32) *pc32 = (unsigned int)wc;
  return r;
}

long __agc_runtime_c32rtomb(long s_addr, unsigned int c32, long ps_addr) {
  return __agc_runtime_wcrtomb(s_addr, (int)c32, ps_addr);
}

long __agc_runtime_mbrlen(long s_addr, long n, long ps_addr) {
  return ag_rt_mbrtowc_stateful(0, s_addr, n,
                                ag_rt_mbstate_at(ps_addr, &ag_rt_mbrlen_state));
}

int __agc_runtime_mbsinit(long ps_addr) {
  struct ag_rt_mbstate *state;
  if (!ps_addr) return 1;
  state = (struct ag_rt_mbstate *)ag_rt_ptr(ps_addr);
  return state->have == 0 && state->need == 0 && state->pending == 0 && state->kind == 0;
}

long __agc_runtime_mbsrtowcs(long dst_addr, long srcp_addr, long len, long ps_addr) {
  long state_addr = ps_addr ? ps_addr : (long)&ag_rt_mbsrtowcs_state;
  long *srcp = (long *)ag_rt_ptr(srcp_addr);
  char *src = ag_rt_ptr(*srcp);
  int *dst = dst_addr ? (int *)ag_rt_ptr(dst_addr) : (int *)0;
  long count = 0;
  long pos = 0;
  while ((!dst || count < len) && src[pos]) {
    int wc = 0;
    long r = __agc_runtime_mbrtowc((long)&wc, (long)(src + pos), 4, state_addr);
    if (r < 0) return r;
    if (dst) dst[count] = wc;
    count++;
    pos += r;
  }
  if (src[pos] == 0) {
    if (dst && count < len) dst[count] = 0;
    *srcp = 0;
  } else if (dst) {
    *srcp = (long)(src + pos);
  }
  return count;
}

long __agc_runtime_wcsrtombs(long dst_addr, long srcp_addr, long len, long ps_addr) {
  long state_addr = ps_addr ? ps_addr : (long)&ag_rt_wcsrtombs_state;
  long *srcp = (long *)ag_rt_ptr(srcp_addr);
  int *src = (int *)ag_rt_ptr(*srcp);
  char *dst = dst_addr ? ag_rt_ptr(dst_addr) : (char *)0;
  long count = 0;
  long bytes = 0;
  while (src[count]) {
    char tmp[4];
    long r = __agc_runtime_wcrtomb((long)tmp, src[count], state_addr);
    if (r < 0) return r;
    if (dst && bytes + r > len) {
      *srcp = (long)(src + count);
      return bytes;
    }
    if (dst) {
      long j = 0;
      while (j < r) {
        dst[bytes + j] = tmp[j];
        j++;
      }
    }
    bytes += r;
    count++;
  }
  if (dst && bytes < len) dst[bytes] = 0;
  if (!dst || bytes < len) {
    *srcp = 0;
  }
  return bytes;
}

static int ag_rt_wcsftime_put_wch(int *dst, long maxsize, long *pos, int ch) {
  if (*pos + 1 >= maxsize) return 0;
  dst[*pos] = ch;
  *pos = *pos + 1;
  return 1;
}

static int ag_rt_wcsftime_put_narrow(int *dst, long maxsize, long *pos, char *s, long n) {
  long i = 0;
  while (i < n) {
    if (!ag_rt_wcsftime_put_wch(dst, maxsize, pos, (unsigned char)s[i])) return 0;
    i++;
  }
  return 1;
}

static int ag_rt_wcsftime_put_format(int *dst, long maxsize, long *pos, int spec, struct ag_rt_tm *tm) {
  char tmp[128];
  long tmp_pos = 0;
  if (!ag_rt_strftime_put_format(tmp, (long)sizeof(tmp), &tmp_pos, spec, tm)) return 0;
  return ag_rt_wcsftime_put_narrow(dst, maxsize, pos, tmp, tmp_pos);
}

unsigned long __agc_runtime_wcsftime(long dst_addr, unsigned long maxsize, long format_addr, long timeptr_addr) {
  int *dst = (int *)ag_rt_ptr(dst_addr);
  int *fmt = (int *)ag_rt_ptr(format_addr);
  struct ag_rt_tm *tm = (struct ag_rt_tm *)ag_rt_ptr(timeptr_addr);
  long pos = 0;
  if (!dst || !fmt || !tm || maxsize == 0) return 0;
  while (*fmt) {
    if (*fmt == '%') {
      fmt++;
      if (!*fmt) {
        if (!ag_rt_wcsftime_put_wch(dst, (long)maxsize, &pos, '%')) return 0;
        break;
      }
      if (!ag_rt_wcsftime_put_format(dst, (long)maxsize, &pos, *fmt, tm)) return 0;
    } else {
      if (!ag_rt_wcsftime_put_wch(dst, (long)maxsize, &pos, *fmt)) return 0;
    }
    fmt++;
  }
  dst[pos] = 0;
  return (unsigned long)pos;
}

int __agc_runtime_mblen(long s_addr, long n) {
  long r;
  if (!s_addr) {
    ag_rt_mbstate_reset(&ag_rt_mblen_state);
    return 0;
  }
  r = ag_rt_mbrtowc_stateful(0, s_addr, n, &ag_rt_mblen_state);
  if (r < 0) return -1;
  return (int)r;
}

int __agc_runtime_mbtowc(long pwc_addr, long s_addr, long n) {
  long r;
  if (!s_addr) {
    ag_rt_mbstate_reset(&ag_rt_mbtowc_state);
    return 0;
  }
  r = ag_rt_mbrtowc_stateful(pwc_addr, s_addr, n, &ag_rt_mbtowc_state);
  if (r < 0) return -1;
  return (int)r;
}

int __agc_runtime_wctomb(long s_addr, int wc) {
  long r;
  if (!s_addr) {
    ag_rt_mbstate_reset(&ag_rt_wctomb_state);
    return 0;
  }
  r = __agc_runtime_wcrtomb(s_addr, wc, (long)&ag_rt_wctomb_state);
  if (r < 0) return -1;
  return (int)r;
}

long __agc_runtime_mbstowcs(long dst_addr, long src_addr, long n) {
  long srcp = src_addr;
  if (!src_addr) return -1;
  return __agc_runtime_mbsrtowcs(dst_addr, (long)&srcp, n, 0);
}

long __agc_runtime_wcstombs(long dst_addr, long src_addr, long n) {
  long srcp = src_addr;
  if (!src_addr) return -1;
  return __agc_runtime_wcsrtombs(dst_addr, (long)&srcp, n, 0);
}

int __agc_runtime_btowc(int c) {
  return c == -1 ? -1 : (c & 255);
}

int __agc_runtime_wctob(int c) {
  return (c >= 0 && c <= 255) ? c : -1;
}

int __agc_runtime_fputwc(int wc, long stream_addr) {
  char tmp[4];
  long n = __agc_runtime_wcrtomb((long)tmp, wc, 0);
  long i = 0;
  if (!ag_rt_stream_orientation(stream_addr)) {
    ag_rt_set_errno(AG_RT_EBADF);
    return -1;
  }
  (void)ag_rt_orient_stream(stream_addr, 1);
  if (n < 0) return -1;
  while (i < n) {
    if (__agc_runtime_fputc((unsigned char)tmp[i], stream_addr) < 0) return -1;
    i++;
  }
  return wc;
}

int __agc_runtime_putwc(int wc, long stream_addr) {
  return __agc_runtime_fputwc(wc, stream_addr);
}

int __agc_runtime_putwchar(int wc) {
  return __agc_runtime_fputwc(wc, (long)__stdoutp);
}

static int ag_rt_utf8_need_from_first(int c) {
  if (c < 0) return -1;
  if ((c & 0x80) == 0) return 1;
  if ((c & 0xe0) == 0xc0) return 2;
  if ((c & 0xf0) == 0xe0) return 3;
  if ((c & 0xf8) == 0xf0) return 4;
  return -1;
}

int __agc_runtime_fgetwc(long stream_addr) {
  char tmp[4];
  int wc = 0;
  int first = __agc_runtime_fgetc(stream_addr);
  int need = ag_rt_utf8_need_from_first(first);
  int i = 1;
  long r;
  if (!ag_rt_stream_orientation(stream_addr)) {
    ag_rt_set_errno(AG_RT_EBADF);
    return -1;
  }
  (void)ag_rt_orient_stream(stream_addr, 1);
  if (first < 0 || need < 0) return -1;
  tmp[0] = (char)first;
  while (i < need) {
    int ch = __agc_runtime_fgetc(stream_addr);
    if (ch < 0) return -1;
    tmp[i] = (char)ch;
    i++;
  }
  r = __agc_runtime_mbrtowc((long)&wc, (long)tmp, need, 0);
  if (r < 0) return -1;
  return wc;
}

int __agc_runtime_getwc(long stream_addr) {
  return __agc_runtime_fgetwc(stream_addr);
}

int __agc_runtime_getwchar(void) {
  return __agc_runtime_fgetwc((long)&ag_rt_file_value);
}

int __agc_runtime_ungetwc(int wc, long stream_addr) {
  if (wc < 0 || wc > 0x7f) return -1;
  return __agc_runtime_ungetc(wc, stream_addr);
}

long __agc_runtime_fgetws(long s_addr, int n, long stream_addr) {
  int *dst = (int *)ag_rt_ptr(s_addr);
  int i = 0;
  int wc;
  if (!dst || n <= 0) return 0;
  while (i + 1 < n) {
    wc = __agc_runtime_fgetwc(stream_addr);
    if (wc < 0) break;
    dst[i++] = wc;
    if (wc == '\n') break;
  }
  if (i == 0) return 0;
  dst[i] = 0;
  return s_addr;
}

int __agc_runtime_fputws(long s_addr, long stream_addr) {
  int *s = (int *)ag_rt_ptr(s_addr);
  int count = 0;
  if (!s) return -1;
  while (*s) {
    if (__agc_runtime_fputwc(*s, stream_addr) < 0) return -1;
    s++;
    count++;
  }
  return count;
}

int __agc_runtime_fwide(long stream_addr, int mode) {
  return ag_rt_orient_stream(stream_addr, mode);
}
