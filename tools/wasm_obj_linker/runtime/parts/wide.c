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

long __agc_runtime_mbrtoc16(long pc16_addr, long s_addr, long n, long ps_addr) {
  (void)ps_addr;
  if (!s_addr) return 0;
  if (n == 0) return -2;
  char *s = ag_rt_ptr(s_addr);
  unsigned short *pc16 = (unsigned short *)ag_rt_ptr(pc16_addr);
  if (pc16) *pc16 = (unsigned short)(unsigned char)s[0];
  return s[0] == 0 ? 0 : 1;
}

long __agc_runtime_c16rtomb(long s_addr, int c16, long ps_addr) {
  (void)ps_addr;
  if (!s_addr) return 1;
  char *s = ag_rt_ptr(s_addr);
  s[0] = (char)c16;
  return 1;
}

long __agc_runtime_mbrtoc32(long pc32_addr, long s_addr, long n, long ps_addr) {
  (void)ps_addr;
  if (!s_addr) return 0;
  if (n == 0) return -2;
  char *s = ag_rt_ptr(s_addr);
  unsigned int *pc32 = (unsigned int *)ag_rt_ptr(pc32_addr);
  if (pc32) *pc32 = (unsigned int)(unsigned char)s[0];
  return s[0] == 0 ? 0 : 1;
}

long __agc_runtime_c32rtomb(long s_addr, unsigned int c32, long ps_addr) {
  (void)ps_addr;
  if (!s_addr) return 1;
  char *s = ag_rt_ptr(s_addr);
  s[0] = (char)c32;
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
