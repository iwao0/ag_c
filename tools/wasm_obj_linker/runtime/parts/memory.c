long __agc_runtime_malloc(long size) {
  long requested = size > 0 ? size : 1;
  long aligned = (requested + 7) & -8;
  long header = ag_rt_heap;
  ag_rt_heap = ag_rt_heap + aligned + 8;
  long *meta = (long *)ag_rt_ptr(header);
  *meta = aligned;
  return header + 8;
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
  long old_size = *(long *)ag_rt_ptr(ptr - 8);
  long copy_size = old_size < size ? old_size : size;
  __agc_runtime_memcpy(p, ptr, copy_size);
  return p;
}

void __agc_runtime_qsort(long base_addr, long nmemb, long size, long compar_addr) {
  if (!base_addr || nmemb <= 1 || size <= 0 || !compar_addr) return;
  int (*cmp)(void *, void *) = (int (*)(void *, void *))compar_addr;
  long tmp_addr = __agc_runtime_malloc(size);
  long i = 0;
  while (i < nmemb) {
    long j = i + 1;
    while (j < nmemb) {
      long a = base_addr + i * size;
      long b = base_addr + j * size;
      if (cmp((char *)a, (char *)b) > 0) {
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
  int (*cmp)(void *, void *) = (int (*)(void *, void *))compar_addr;
  long i = 0;
  while (i < nmemb) {
    long elem = base_addr + i * size;
    int r = cmp((char *)key_addr, (char *)elem);
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
