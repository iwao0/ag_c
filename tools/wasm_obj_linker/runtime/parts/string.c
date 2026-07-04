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

int __agc_runtime_strcoll(long a_addr, long b_addr) {
  return __agc_runtime_strcmp(a_addr, b_addr);
}

long __agc_runtime_strxfrm(long dst_addr, long src_addr, long n) {
  char *dst = ag_rt_ptr(dst_addr);
  char *src = ag_rt_ptr(src_addr);
  long len = __agc_runtime_strlen(src_addr);
  long i = 0;
  if (n > 0) {
    while (i + 1 < n && src[i]) {
      dst[i] = src[i];
      i++;
    }
    dst[i] = 0;
  }
  return len;
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

static int ag_rt_str_contains(char *s, int ch) {
  int needle = ch & 255;
  long i = 0;
  while (s[i]) {
    if (((int)s[i] & 255) == needle) return 1;
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

long __agc_runtime_strspn(long s_addr, long accept_addr) {
  char *s = ag_rt_ptr(s_addr);
  char *accept = ag_rt_ptr(accept_addr);
  long i = 0;
  while (s[i] && ag_rt_str_contains(accept, s[i])) i++;
  return i;
}

long __agc_runtime_strcspn(long s_addr, long reject_addr) {
  char *s = ag_rt_ptr(s_addr);
  char *reject = ag_rt_ptr(reject_addr);
  long i = 0;
  while (s[i] && !ag_rt_str_contains(reject, s[i])) i++;
  return i;
}

long __agc_runtime_strpbrk(long s_addr, long accept_addr) {
  char *s = ag_rt_ptr(s_addr);
  char *accept = ag_rt_ptr(accept_addr);
  long i = 0;
  while (s[i]) {
    if (ag_rt_str_contains(accept, s[i])) return s_addr + i;
    i++;
  }
  return 0;
}

static int ag_rt_strtok_is_delim(char ch, char *delim) {
  return ag_rt_str_contains(delim, ch);
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
