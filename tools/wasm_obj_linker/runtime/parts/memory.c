static long ag_rt_memory_limit(void) {
  return ag_rt_memory_limit_bytes;
}

static long ag_rt_long_max(void) {
  return (long)((unsigned long)-1 >> 1);
}

#define AG_RT_BLOCK_ALLOCATED 1L
#define AG_RT_BLOCK_HEADER_SIZE (5L * (long)sizeof(long))
#define AG_RT_BLOCK_PREFIX_SIZE ((long)sizeof(long))
#define AG_RT_MIN_BLOCK_SIZE \
  (AG_RT_BLOCK_HEADER_SIZE + AG_RT_BLOCK_PREFIX_SIZE + 8L)

/* Headers hold size/state and physical predecessor size. The last three words
   hold allocation metadata or free-list links. A user pointer has its header
   address in the word immediately before it. */
static long ag_rt_heap_base;
static long ag_rt_free_head;
static long ag_rt_last_block;

static long *ag_rt_block_meta(long header) {
  return (long *)ag_rt_ptr(header);
}

static long ag_rt_block_total(long header) {
  return ag_rt_block_meta(header)[0] & -8L;
}

static int ag_rt_block_is_allocated(long header) {
  return (ag_rt_block_meta(header)[0] & AG_RT_BLOCK_ALLOCATED) != 0;
}

static void ag_rt_allocator_init(void) {
  if (ag_rt_heap_base) return;
  ag_rt_heap = (ag_rt_heap + 7) & -8L;
  ag_rt_heap_base = ag_rt_heap;
  ag_rt_free_head = 0;
  ag_rt_last_block = 0;
}

static int ag_rt_alignment_ok(long alignment) {
  if (alignment <= 0) return 0;
  return (alignment & (alignment - 1)) == 0;
}

static int ag_rt_memory_span_fits(long start, long size) {
  long limit = ag_rt_memory_limit();
  return start >= 0 && size >= 0 && start <= limit && size <= limit - start;
}

static int ag_rt_block_layout(long header, long requested, long alignment,
                              long *user_offset_out, long *total_out) {
  long max = ag_rt_long_max();
  long user_start;
  long user;
  long used;
  long total;
  if (requested <= 0 || alignment < 8 || !ag_rt_alignment_ok(alignment)) return 0;
  if (header < 0 || header > max - AG_RT_BLOCK_HEADER_SIZE - AG_RT_BLOCK_PREFIX_SIZE)
    return 0;
  user_start = header + AG_RT_BLOCK_HEADER_SIZE + AG_RT_BLOCK_PREFIX_SIZE;
  if (user_start > max - (alignment - 1)) return 0;
  user = (user_start + alignment - 1) & -alignment;
  if (user < header || requested > max - (user - header)) return 0;
  used = (user - header) + requested;
  if (used > max - 7) return 0;
  total = (used + 7) & -8L;
  if (total < AG_RT_MIN_BLOCK_SIZE) total = AG_RT_MIN_BLOCK_SIZE;
  *user_offset_out = user - header;
  *total_out = total;
  return 1;
}

static void ag_rt_free_remove(long header) {
  long *meta = ag_rt_block_meta(header);
  long prev = meta[2];
  long next = meta[3];
  if (prev) ag_rt_block_meta(prev)[3] = next;
  else ag_rt_free_head = next;
  if (next) ag_rt_block_meta(next)[2] = prev;
  meta[2] = 0;
  meta[3] = 0;
}

static void ag_rt_free_insert(long header) {
  long *meta = ag_rt_block_meta(header);
  meta[2] = 0;
  meta[3] = ag_rt_free_head;
  meta[4] = 0;
  if (ag_rt_free_head) ag_rt_block_meta(ag_rt_free_head)[2] = header;
  ag_rt_free_head = header;
}

static void ag_rt_update_following_prev(long header) {
  long next = header + ag_rt_block_total(header);
  if (next < ag_rt_heap) ag_rt_block_meta(next)[1] = ag_rt_block_total(header);
}

static void ag_rt_split_allocated_block(long header, long used_total) {
  long old_total = ag_rt_block_total(header);
  long remainder_total = old_total - used_total;
  long old_next = header + old_total;
  long remainder;
  long *remainder_meta;
  if (remainder_total < AG_RT_MIN_BLOCK_SIZE) {
    ag_rt_block_meta(header)[0] = old_total | AG_RT_BLOCK_ALLOCATED;
    return;
  }
  remainder = header + used_total;
  remainder_meta = ag_rt_block_meta(remainder);
  remainder_meta[0] = remainder_total;
  remainder_meta[1] = used_total;
  remainder_meta[2] = 0;
  remainder_meta[3] = 0;
  remainder_meta[4] = 0;
  ag_rt_block_meta(header)[0] = used_total | AG_RT_BLOCK_ALLOCATED;

  if (old_next < ag_rt_heap && !ag_rt_block_is_allocated(old_next)) {
    long next_total = ag_rt_block_total(old_next);
    ag_rt_free_remove(old_next);
    remainder_total += next_total;
    remainder_meta[0] = remainder_total;
    if (ag_rt_last_block == old_next) ag_rt_last_block = remainder;
  } else if (ag_rt_last_block == header) {
    ag_rt_last_block = remainder;
  }
  ag_rt_update_following_prev(remainder);
  ag_rt_free_insert(remainder);
}

static long ag_rt_coalesce_free_block(long header) {
  long *meta = ag_rt_block_meta(header);
  long total = ag_rt_block_total(header);
  long next = header + total;
  if (next < ag_rt_heap && !ag_rt_block_is_allocated(next)) {
    long next_total = ag_rt_block_total(next);
    ag_rt_free_remove(next);
    total += next_total;
    meta[0] = total;
    if (ag_rt_last_block == next) ag_rt_last_block = header;
  }
  if (meta[1] > 0) {
    long prev = header - meta[1];
    if (prev >= ag_rt_heap_base && !ag_rt_block_is_allocated(prev)) {
      long prev_total = ag_rt_block_total(prev);
      ag_rt_free_remove(prev);
      total += prev_total;
      ag_rt_block_meta(prev)[0] = total;
      if (ag_rt_last_block == header) ag_rt_last_block = prev;
      header = prev;
    }
  }
  ag_rt_update_following_prev(header);
  return header;
}

static long ag_rt_alloc_block(long requested, long alignment) {
  long free_block;
  long user_offset = 0;
  long needed = 0;
  long header;
  long prev_total;
  long *meta;
  ag_rt_allocator_init();

  free_block = ag_rt_free_head;
  while (free_block) {
    long next_free = ag_rt_block_meta(free_block)[3];
    if (ag_rt_block_layout(free_block, requested, alignment,
                           &user_offset, &needed) &&
        ag_rt_block_total(free_block) >= needed) {
      ag_rt_free_remove(free_block);
      ag_rt_split_allocated_block(free_block, needed);
      meta = ag_rt_block_meta(free_block);
      meta[2] = requested;
      meta[3] = user_offset;
      meta[4] = alignment;
      *(long *)ag_rt_ptr(free_block + user_offset - AG_RT_BLOCK_PREFIX_SIZE) =
          free_block;
      return free_block + user_offset;
    }
    free_block = next_free;
  }

  header = ag_rt_heap;
  if (!ag_rt_block_layout(header, requested, alignment, &user_offset, &needed)) return 0;
  if (!ag_rt_memory_span_fits(header, needed)) return 0;
  prev_total = ag_rt_last_block ? ag_rt_block_total(ag_rt_last_block) : 0;
  meta = ag_rt_block_meta(header);
  meta[0] = needed | AG_RT_BLOCK_ALLOCATED;
  meta[1] = prev_total;
  meta[2] = requested;
  meta[3] = user_offset;
  meta[4] = alignment;
  *(long *)ag_rt_ptr(header + user_offset - AG_RT_BLOCK_PREFIX_SIZE) = header;
  ag_rt_heap = header + needed;
  ag_rt_last_block = header;
  return header + user_offset;
}

static int ag_rt_allocated_header(long ptr, long *header_out) {
  long header;
  long total;
  long *meta;
  if (!ptr || !ag_rt_heap_base || ptr < ag_rt_heap_base + AG_RT_BLOCK_PREFIX_SIZE ||
      ptr > ag_rt_heap) return 0;
  header = *(long *)ag_rt_ptr(ptr - AG_RT_BLOCK_PREFIX_SIZE);
  if (header < ag_rt_heap_base || header > ag_rt_heap - AG_RT_BLOCK_HEADER_SIZE ||
      (header & 7) != 0) return 0;
  meta = ag_rt_block_meta(header);
  total = ag_rt_block_total(header);
  if (!ag_rt_block_is_allocated(header) || total < AG_RT_MIN_BLOCK_SIZE ||
      header > ag_rt_heap - total || meta[3] < AG_RT_BLOCK_HEADER_SIZE + AG_RT_BLOCK_PREFIX_SIZE ||
      ptr != header + meta[3] || meta[2] <= 0 || meta[2] > total - meta[3]) return 0;
  *header_out = header;
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
  long requested = size_value > 0 ? (long)size_value : 1;
  return (void *)ag_rt_alloc_block(requested, 8);
}

void *__agc_runtime_aligned_alloc(unsigned long alignment_value,
                                  unsigned long size_value) {
  if (alignment_value > (unsigned long)ag_rt_long_max() ||
      size_value > (unsigned long)ag_rt_long_max()) return 0;
  long alignment = (long)alignment_value;
  long size = (long)size_value;
  if (!ag_rt_alignment_ok(alignment) || size < 0) return 0;
  if (alignment < 8) alignment = 8;
  if (size % alignment != 0) return 0;
  return (void *)ag_rt_alloc_block(size > 0 ? size : 1, alignment);
}

void __agc_runtime_free(void *ptr) {
  long pointer = (long)ptr;
  long header;
  long *meta;
  if (!pointer) return;
  if (!ag_rt_allocated_header(pointer, &header)) return;
  meta = ag_rt_block_meta(header);
  meta[0] = ag_rt_block_total(header);
  meta[2] = 0;
  meta[3] = 0;
  meta[4] = 0;
  header = ag_rt_coalesce_free_block(header);
  ag_rt_free_insert(header);
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
  long requested = size_value > 0 ? (long)size_value : 1;
  long header = 0;
  long *meta;
  long old_requested;
  long old_total;
  long user_offset;
  long alignment;
  long needed = 0;
  long ignored_offset = 0;
  long next;
  if (!ptr) return __agc_runtime_malloc(size_value);
  if (size_value == 0) {
    __agc_runtime_free(pointer);
    return 0;
  }
  if (!ag_rt_allocated_header(ptr, &header)) return 0;
  meta = ag_rt_block_meta(header);
  old_requested = meta[2];
  old_total = ag_rt_block_total(header);
  user_offset = meta[3];
  alignment = meta[4] >= 8 ? meta[4] : 8;
  if (!ag_rt_block_layout(header, requested, alignment, &ignored_offset, &needed) ||
      ignored_offset != user_offset) return 0;

  if (needed <= old_total) {
    meta[2] = requested;
    ag_rt_split_allocated_block(header, needed);
    return pointer;
  }

  next = header + old_total;
  if (next < ag_rt_heap && !ag_rt_block_is_allocated(next)) {
    long next_total = ag_rt_block_total(next);
    long combined = old_total + next_total;
    int next_is_tail = next + next_total == ag_rt_heap;
    if (combined >= needed ||
        (next_is_tail && ag_rt_memory_span_fits(header, needed))) {
      ag_rt_free_remove(next);
      if (combined < needed) {
        ag_rt_heap = header + needed;
        combined = needed;
      }
      meta[0] = combined | AG_RT_BLOCK_ALLOCATED;
      meta[2] = requested;
      if (ag_rt_last_block == next) ag_rt_last_block = header;
      ag_rt_split_allocated_block(header, needed);
      return pointer;
    }
  } else if (next == ag_rt_heap && ag_rt_memory_span_fits(header, needed)) {
    ag_rt_heap = header + needed;
    meta[0] = needed | AG_RT_BLOCK_ALLOCATED;
    meta[2] = requested;
    return pointer;
  }

  long p = ag_rt_alloc_block(requested, alignment);
  if (!p) return 0;
  long copy_size = old_requested < requested ? old_requested : requested;
  __agc_runtime_memcpy(p, ptr, copy_size);
  __agc_runtime_free(pointer);
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
  __agc_runtime_free((void *)tmp_addr);
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
