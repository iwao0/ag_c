static long ag_rt_memory_limit(void) {
  return ag_rt_memory_limit_bytes;
}

static long ag_rt_long_max(void) {
  return (long)((unsigned long)-1 >> 1);
}

static int ag_rt_alloc_size_ok(long size, long *aligned_out) {
  long requested;
  long aligned;
  if (size < 0) return 0;
  requested = size > 0 ? size : 1;
  if (requested > ag_rt_long_max() - 15) return 0;
  aligned = (requested + 7) & -8;
  *aligned_out = aligned;
  return 1;
}

static int ag_rt_array_span_ok(long base_addr, long nmemb, long size) {
  long last_off;
  if (base_addr < 0 || nmemb < 0 || size <= 0) return 0;
  if (base_addr > ag_rt_memory_limit()) return 0;
  if (nmemb <= 1) return 1;
  if (size > ag_rt_long_max() / (nmemb - 1)) return 0;
  last_off = size * (nmemb - 1);
  return base_addr <= ag_rt_memory_limit() - last_off;
}

void *__agc_runtime_malloc(unsigned long size_value) {
  if (size_value > (unsigned long)ag_rt_long_max()) return 0;
  long size = (long)size_value;
  long aligned = 0;
  if (!ag_rt_alloc_size_ok(size, &aligned)) return 0;
  if (ag_rt_heap > ag_rt_memory_limit() - aligned - 8) return 0;
  long header = ag_rt_heap;
  ag_rt_heap = ag_rt_heap + aligned + 8;
  long *meta = (long *)ag_rt_ptr(header);
  *meta = aligned;
  return (void *)(header + 8);
}

static int ag_rt_alignment_ok(long alignment) {
  if (alignment <= 0) return 0;
  return (alignment & (alignment - 1)) == 0;
}

void *__agc_runtime_aligned_alloc(unsigned long alignment_value,
                                  unsigned long size_value) {
  if (alignment_value > (unsigned long)ag_rt_long_max() ||
      size_value > (unsigned long)ag_rt_long_max()) return 0;
  long alignment = (long)alignment_value;
  long size = (long)size_value;
  long requested;
  long raw;
  long aligned_ptr;
  long header;
  long total;
  if (!ag_rt_alignment_ok(alignment) || size < 0) return 0;
  if (alignment < 8) alignment = 8;
  if (size % alignment != 0) return 0;
  requested = size > 0 ? size : 1;
  if (requested > ag_rt_long_max() - alignment - 8) return 0;
  raw = ag_rt_heap + 8;
  aligned_ptr = (raw + alignment - 1) & -alignment;
  header = aligned_ptr - 8;
  total = (aligned_ptr + requested) - ag_rt_heap;
  if (total < 0 || ag_rt_heap > ag_rt_memory_limit() - total) return 0;
  ag_rt_heap = ag_rt_heap + total;
  *(long *)ag_rt_ptr(header) = requested;
  return (void *)aligned_ptr;
}

void __agc_runtime_free(void *ptr) {
  (void)ptr;
}

void *__agc_runtime_calloc(unsigned long nmemb_value, unsigned long size_value) {
  if (nmemb_value > (unsigned long)ag_rt_long_max() ||
      size_value > (unsigned long)ag_rt_long_max()) return 0;
  long nmemb = (long)nmemb_value;
  long size = (long)size_value;
  if (nmemb < 0 || size < 0) return 0;
  if (size != 0 && nmemb > ag_rt_long_max() / size) return 0;
  long n = nmemb * size;
  long p = (long)__agc_runtime_malloc((unsigned long)n);
  if (!p) return 0;
  char *dst = ag_rt_ptr(p);
  long i = 0;
  while (i < n) dst[i++] = 0;
  return (void *)p;
}

void *__agc_runtime_realloc(void *pointer, unsigned long size_value) {
  if (size_value > (unsigned long)ag_rt_long_max()) return 0;
  long ptr = (long)pointer;
  long size = (long)size_value;
  if (!ptr) return __agc_runtime_malloc(size_value);
  if (size == 0) return 0;
  if (ptr < 8) return 0;
  if (ptr > ag_rt_memory_limit()) {
    long external = (long)__agc_runtime_malloc(size_value);
    if (!external) return 0;
    __agc_runtime_memcpy(external, ptr, size);
    return (void *)external;
  }
  long old_size = *(long *)ag_rt_ptr(ptr - 8);
  if (old_size < 0 || ptr > ag_rt_memory_limit() - old_size) {
    long external = (long)__agc_runtime_malloc(size_value);
    if (!external) return 0;
    __agc_runtime_memcpy(external, ptr, size);
    return (void *)external;
  }
  long p = (long)__agc_runtime_malloc(size_value);
  if (!p) return 0;
  long copy_size = old_size < size ? old_size : size;
  __agc_runtime_memcpy(p, ptr, copy_size);
  return (void *)p;
}

void __agc_runtime_qsort(
    void *base_pointer, unsigned long nmemb_value, unsigned long size_value,
    int (*compar)(const void *, const void *)) {
  long base_addr = (long)base_pointer;
  long nmemb = (long)nmemb_value;
  long size = (long)size_value;
  long compar_addr = (long)compar;
  if (!base_addr || nmemb <= 1 || size <= 0 || !compar_addr) return;
  if (!ag_rt_array_span_ok(base_addr, nmemb, size)) return;
  int (*cmp)(const void *, const void *) = compar;
  long tmp_addr = (long)__agc_runtime_malloc((unsigned long)size);
  if (!tmp_addr) return;
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

void *__agc_runtime_bsearch(
    const void *key_pointer, const void *base_pointer,
    unsigned long nmemb_value, unsigned long size_value,
    int (*compar)(const void *, const void *)) {
  long key_addr = (long)key_pointer;
  long base_addr = (long)base_pointer;
  long nmemb = (long)nmemb_value;
  long size = (long)size_value;
  long compar_addr = (long)compar;
  if (!key_addr || !base_addr || nmemb <= 0 || size <= 0 || !compar_addr) return 0;
  if (!ag_rt_array_span_ok(base_addr, nmemb, size)) return 0;
  int (*cmp)(const void *, const void *) = compar;
  long i = 0;
  while (i < nmemb) {
    long elem = base_addr + i * size;
    int r = cmp((char *)key_addr, (char *)elem);
    if (r == 0) return (void *)elem;
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
