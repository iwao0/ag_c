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

static int ag_rt_wide_int_digit(int c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'z') return c - 'a' + 10;
  if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
  return -1;
}

static int *__agc_runtime_wint_prefix(int *s, int *base) {
  int b = *base;
  if (b == 0) {
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X') &&
        ag_rt_wide_int_digit(s[2]) >= 0 && ag_rt_wide_int_digit(s[2]) < 16) {
      *base = 16;
      return s + 2;
    }
    if (s[0] == '0') {
      *base = 8;
      return s;
    }
    *base = 10;
    return s;
  }
  if (b == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X') &&
      ag_rt_wide_int_digit(s[2]) >= 0 && ag_rt_wide_int_digit(s[2]) < 16) {
    return s + 2;
  }
  return s;
}

long __agc_runtime_wcstol(long nptr_addr, long endptr_addr, int base) {
  int *orig = (int *)ag_rt_ptr(nptr_addr);
  int *s = orig;
  while (*s == ' ' || *s == '\f' || *s == '\n' || *s == '\r' || *s == '\t' || *s == '\v') s++;
  int sign = 1;
  if (*s == '-') {
    sign = -1;
    s++;
  } else if (*s == '+') {
    s++;
  }
  s = __agc_runtime_wint_prefix(s, &base);
  int *digits = s;
  long acc = 0;
  for (;;) {
    int digit = ag_rt_wide_int_digit(*s);
    if (digit < 0) break;
    if (digit >= base) break;
    acc = acc * base + digit;
    s++;
  }
  if (endptr_addr) {
    long *endp = (long *)ag_rt_ptr(endptr_addr);
    *endp = (long)(s == digits ? orig : s);
  }
  return sign * acc;
}

unsigned long __agc_runtime_wcstoul(long nptr_addr, long endptr_addr, int base) {
  return (unsigned long)__agc_runtime_wcstol(nptr_addr, endptr_addr, base);
}

double __agc_runtime_wcstod(long nptr_addr, long endptr_addr) {
  int *orig = (int *)ag_rt_ptr(nptr_addr);
  int *s = orig;
  while (*s == ' ' || *s == '\f' || *s == '\n' || *s == '\r' || *s == '\t' || *s == '\v') s++;
  double sign = 1.0;
  if (*s == '-') {
    sign = -1.0;
    s++;
  } else if (*s == '+') {
    s++;
  }
  double acc = 0.0;
  int have_digit = 0;
  while (*s >= '0' && *s <= '9') {
    have_digit = 1;
    acc = acc * 10.0 + (double)(*s - '0');
    s++;
  }
  if (*s == '.') {
    double place = 0.1;
    s++;
    while (*s >= '0' && *s <= '9') {
      have_digit = 1;
      acc = acc + (double)(*s - '0') * place;
      place = place / 10.0;
      s++;
    }
  }
  if (!have_digit) {
    if (endptr_addr) {
      long *endp = (long *)ag_rt_ptr(endptr_addr);
      *endp = (long)orig;
    }
    return 0.0;
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
  unsigned char b0 = (unsigned char)s[0];
  int wc = 0;
  long need = 1;
  if (b0 == 0) {
    if (pwc) *pwc = 0;
    return 0;
  } else if (b0 < 0x80) {
    wc = b0;
  } else if ((b0 & 0xe0) == 0xc0) {
    need = 2;
    if (n < need) return -2;
    unsigned char b1 = (unsigned char)s[1];
    if ((b1 & 0xc0) != 0x80) return -1;
    wc = ((b0 & 0x1f) << 6) | (b1 & 0x3f);
  } else if ((b0 & 0xf0) == 0xe0) {
    need = 3;
    if (n < need) return -2;
    unsigned char b1 = (unsigned char)s[1];
    unsigned char b2 = (unsigned char)s[2];
    if ((b1 & 0xc0) != 0x80 || (b2 & 0xc0) != 0x80) return -1;
    wc = ((b0 & 0x0f) << 12) | ((b1 & 0x3f) << 6) | (b2 & 0x3f);
  } else if ((b0 & 0xf8) == 0xf0) {
    need = 4;
    if (n < need) return -2;
    unsigned char b1 = (unsigned char)s[1];
    unsigned char b2 = (unsigned char)s[2];
    unsigned char b3 = (unsigned char)s[3];
    if ((b1 & 0xc0) != 0x80 || (b2 & 0xc0) != 0x80 || (b3 & 0xc0) != 0x80) return -1;
    wc = ((b0 & 0x07) << 18) | ((b1 & 0x3f) << 12) | ((b2 & 0x3f) << 6) | (b3 & 0x3f);
  } else {
    return -1;
  }
  if (pwc) *pwc = wc;
  return need;
}

long __agc_runtime_wcrtomb(long s_addr, int wc, long ps_addr) {
  (void)ps_addr;
  if (!s_addr) return 1;
  char *s = ag_rt_ptr(s_addr);
  if (wc < 0) return -1;
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
  (void)ps_addr;
  if (!s_addr) return 0;
  if (n == 0) return -2;
  int wc = 0;
  long r = __agc_runtime_mbrtowc((long)&wc, s_addr, n, 0);
  if (r < 0) return r;
  unsigned short *pc16 = (unsigned short *)ag_rt_ptr(pc16_addr);
  if (pc16) *pc16 = (unsigned short)wc;
  return r;
}

long __agc_runtime_c16rtomb(long s_addr, int c16, long ps_addr) {
  return __agc_runtime_wcrtomb(s_addr, c16, ps_addr);
}

long __agc_runtime_mbrtoc32(long pc32_addr, long s_addr, long n, long ps_addr) {
  (void)ps_addr;
  if (!s_addr) return 0;
  if (n == 0) return -2;
  int wc = 0;
  long r = __agc_runtime_mbrtowc((long)&wc, s_addr, n, 0);
  if (r < 0) return r;
  unsigned int *pc32 = (unsigned int *)ag_rt_ptr(pc32_addr);
  if (pc32) *pc32 = (unsigned int)wc;
  return r;
}

long __agc_runtime_c32rtomb(long s_addr, unsigned int c32, long ps_addr) {
  return __agc_runtime_wcrtomb(s_addr, (int)c32, ps_addr);
}

long __agc_runtime_mbsrtowcs(long dst_addr, long srcp_addr, long len, long ps_addr) {
  (void)ps_addr;
  long *srcp = (long *)ag_rt_ptr(srcp_addr);
  char *src = ag_rt_ptr(*srcp);
  int *dst = dst_addr ? (int *)ag_rt_ptr(dst_addr) : (int *)0;
  long count = 0;
  long pos = 0;
  while ((!dst || count < len) && src[pos]) {
    int wc = 0;
    long r = __agc_runtime_mbrtowc((long)&wc, (long)(src + pos), 4, 0);
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
  (void)ps_addr;
  long *srcp = (long *)ag_rt_ptr(srcp_addr);
  int *src = (int *)ag_rt_ptr(*srcp);
  char *dst = dst_addr ? ag_rt_ptr(dst_addr) : (char *)0;
  long count = 0;
  long bytes = 0;
  while (src[count]) {
    char tmp[4];
    long r = __agc_runtime_wcrtomb((long)tmp, src[count], 0);
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

int __agc_runtime_btowc(int c) {
  return c == -1 ? -1 : (c & 255);
}

int __agc_runtime_wctob(int c) {
  return (c >= 0 && c <= 255) ? c : -1;
}
