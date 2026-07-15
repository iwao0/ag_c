int __agc_runtime_fgetc(long stream_addr);

int __agc_runtime_putchar(int c) {
  return ag_rt_stdout_write_mem((char *)&c, 1) == 1 ? c : -1;
}

int __agc_runtime_puts(long s_addr) {
  char *s = ag_rt_ptr(s_addr);
  long n = __agc_runtime_strlen(s_addr);
  if (ag_rt_stdout_write_str(s) != n ||
      ag_rt_stdout_write_mem("\n", 1) != 1) return -1;
  return (int)n + 1;
}

static long ag_rt_file_write_mem(struct ag_rt_file *f, char *src, long total) {
  long i = 0;
  long gap;
  char *dst;
  long *lenp;
  if (total <= 0) return 0;
  if (!f) {
    ag_rt_set_errno(9);
    return 0;
  }
  if (!ag_rt_file_can_write(f)) {
    f->error = 1;
    ag_rt_set_errno(9);
    return 0;
  }
  if (f->store_index < 0 || f->store_index >= AG_RT_FILE_STORE_COUNT ||
      !ag_rt_file_stores[f->store_index].used) {
    f->error = 1;
    ag_rt_set_errno(9);
    return 0;
  }
  if (f->append_mode) f->pos = ag_rt_file_stores[f->store_index].len;
  dst = ag_rt_store_buf_for_write(f->store_index, f->pos + total);
  if (!dst) {
    f->error = 1;
    return 0;
  }
  lenp = &ag_rt_file_stores[f->store_index].len;
  gap = *lenp;
  while (gap < f->pos && gap < AG_RT_FILE_BUF_CAP) {
    dst[gap++] = 0;
  }
  while (i < total && f->pos < AG_RT_FILE_BUF_CAP) {
    dst[f->pos++] = src[i++];
  }
  if (i < total) {
    f->error = 1;
    ag_rt_set_errno(12);
  }
  if (i > 0 && f->pos > *lenp) *lenp = f->pos;
  ag_rt_file_set_pos(f, f->pos);
  return i;
}

int __agc_runtime_fputs(long s_addr, long stream_addr) {
  char *s = ag_rt_ptr(s_addr);
  long n = __agc_runtime_strlen(s_addr);
  (void)ag_rt_orient_stream(stream_addr, -1);
  if (ag_rt_is_stderr_stream(stream_addr)) {
    if (ag_rt_stderr_write_str(s) != n) return -1;
  } else if (ag_rt_is_stdout_stream(stream_addr)) {
    if (ag_rt_stdout_write_str(s) != n) return -1;
  } else {
    return ag_rt_file_write_mem(ag_rt_input_stream(stream_addr), s, n) == n ? (int)n : -1;
  }
  return (int)n;
}

int __agc_runtime_fputc(int c, long stream_addr) {
  char ch = (char)c;
  (void)ag_rt_orient_stream(stream_addr, -1);
  if (ag_rt_is_stderr_stream(stream_addr)) {
    if (ag_rt_stderr_write_char(c) != 1) return -1;
  } else if (ag_rt_is_stdout_stream(stream_addr)) {
    if (ag_rt_stdout_write_mem((char *)&c, 1) != 1) return -1;
  } else if (ag_rt_file_write_mem(ag_rt_input_stream(stream_addr), &ch, 1) != 1) {
    return -1;
  }
  return c;
}

int __agc_runtime_putc(int c, long stream_addr) {
  return __agc_runtime_fputc(c, stream_addr);
}

int __agc_runtime_fflush(long stream_addr) {
  struct ag_rt_file *f;
  if (!stream_addr) return 0;
  if (ag_rt_is_stdout_stream(stream_addr) || ag_rt_is_stderr_stream(stream_addr)) return 0;
  f = ag_rt_input_stream(stream_addr);
  if (!f) {
    ag_rt_set_errno(9);
    return -1;
  }
  if (!ag_rt_stream_has_store(f)) {
    f->error = 1;
    ag_rt_set_errno(9);
    return -1;
  }
  return 0;
}

int __agc_runtime_setvbuf(long stream_addr, long buf_addr, int mode, unsigned long size) {
  struct ag_rt_file *f;
  (void)buf_addr;
  (void)size;
  if (mode < 0 || mode > 2) {
    ag_rt_set_errno(22);
    return -1;
  }
  if (ag_rt_is_stdout_stream(stream_addr) || ag_rt_is_stderr_stream(stream_addr)) {
    if (mode == 2) return 0;
    ag_rt_set_errno(AG_RT_ENOSYS);
    return -1;
  }
  f = ag_rt_input_stream(stream_addr);
  if (!f) {
    ag_rt_set_errno(9);
    return -1;
  }
  if (!ag_rt_stream_has_store(f)) {
    f->error = 1;
    ag_rt_set_errno(9);
    return -1;
  }
  if (mode != 2) {
    ag_rt_set_errno(AG_RT_ENOSYS);
    return -1;
  }
  return 0;
}

void __agc_runtime_setbuf(long stream_addr, long buf_addr) {
  (void)__agc_runtime_setvbuf(stream_addr, buf_addr, buf_addr ? 0 : 2, 8192);
}

long __agc_runtime_stdin_capacity(void) {
  return (long)sizeof(ag_rt_stdin_buf);
}

long __agc_runtime_stdin_write(long ptr_addr, long len) {
  char *src = ag_rt_ptr(ptr_addr);
  long n = len;
  long i = 0;
  if (n < 0) n = 0;
  if (n > (long)sizeof(ag_rt_stdin_buf)) n = (long)sizeof(ag_rt_stdin_buf);
  while (i < n) {
    ag_rt_stdin_buf[i] = src[i];
    i++;
  }
  ag_rt_stdin_len = n;
  ag_rt_reset_files();
  for (int i = 0; i < 8; i++) {
    ag_rt_fds[i].used = 0;
    ag_rt_fds[i].pos = 0;
    ag_rt_fds[i].store_index = -1;
    ag_rt_fds[i].read_mode = 0;
    ag_rt_fds[i].write_mode = 0;
    ag_rt_fds[i].append_mode = 0;
  }
  __stdinp = (void *)&ag_rt_file_value;
  return n;
}

int __agc_runtime_fseek(long stream_addr, long offset, int whence) {
  struct ag_rt_file *f = ag_rt_input_stream(stream_addr);
  long base = 0;
  if (!f) {
    ag_rt_set_errno(9);
    return -1;
  }
  if (!ag_rt_stream_has_store(f)) {
    f->error = 1;
    ag_rt_set_errno(9);
    return -1;
  }
  if (whence == 0) {
    base = 0;
  } else if (whence == 1) {
    base = f->pos;
  } else if (whence == 2) {
    base = ag_rt_stream_len(f);
  } else {
    f->error = 1;
    ag_rt_set_errno(22);
    return -1;
  }
  long next = base + offset;
  if (next < 0) {
    f->error = 1;
    ag_rt_set_errno(22);
    return -1;
  }
  ag_rt_file_set_pos(f, next);
  f->eof = 0;
  return 0;
}

long __agc_runtime_ftell(long stream_addr) {
  struct ag_rt_file *f = ag_rt_input_stream(stream_addr);
  if (!f) {
    ag_rt_set_errno(9);
    return -1;
  }
  if (!ag_rt_stream_has_store(f)) {
    f->error = 1;
    ag_rt_set_errno(9);
    return -1;
  }
  return f->pos;
}

int __agc_runtime_fgetpos(long stream_addr, long pos_addr) {
  struct ag_rt_file *f = ag_rt_input_stream(stream_addr);
  long *pos;
  if (!f) {
    ag_rt_set_errno(9);
    return -1;
  }
  if (!ag_rt_stream_has_store(f)) {
    f->error = 1;
    ag_rt_set_errno(9);
    return -1;
  }
  if (!pos_addr) {
    ag_rt_set_errno(22);
    return -1;
  }
  pos = (long *)ag_rt_ptr(pos_addr);
  *pos = f->pos;
  return 0;
}

int __agc_runtime_fsetpos(long stream_addr, long pos_addr) {
  long *pos;
  if (!pos_addr) {
    ag_rt_set_errno(22);
    return -1;
  }
  pos = (long *)ag_rt_ptr(pos_addr);
  return __agc_runtime_fseek(stream_addr, *pos, 0);
}

void __agc_runtime_rewind(long stream_addr) {
  struct ag_rt_file *f = ag_rt_input_stream(stream_addr);
  if (!f) {
    ag_rt_set_errno(9);
    return;
  }
  if (!ag_rt_stream_has_store(f)) {
    f->error = 1;
    ag_rt_set_errno(9);
    return;
  }
  ag_rt_file_set_pos(f, 0);
  f->eof = 0;
  f->error = 0;
}

void __agc_runtime_perror(long s_addr) {
  char *s = ag_rt_ptr(s_addr);
  if (s && s[0]) {
    ag_rt_stderr_write_str(s);
    ag_rt_stderr_write_str(": ");
  }
  ag_rt_stderr_write_str(ag_rt_strerror_message(ag_rt_errno_value));
  ag_rt_stderr_write_char('\n');
}

int __agc_runtime_feof(long stream_addr) {
  if (ag_rt_is_stdout_stream(stream_addr) || ag_rt_is_stderr_stream(stream_addr)) return 0;
  struct ag_rt_file *f = ag_rt_input_stream(stream_addr);
  if (!f) {
    ag_rt_set_errno(9);
    return 0;
  }
  return f->eof;
}

int __agc_runtime_ferror(long stream_addr) {
  if (ag_rt_is_stdout_stream(stream_addr) || ag_rt_is_stderr_stream(stream_addr)) return 0;
  struct ag_rt_file *f = ag_rt_input_stream(stream_addr);
  if (!f) {
    ag_rt_set_errno(9);
    return 1;
  }
  return f->error;
}

void __agc_runtime_clearerr(long stream_addr) {
  if (ag_rt_is_stdout_stream(stream_addr) || ag_rt_is_stderr_stream(stream_addr)) return;
  struct ag_rt_file *f = ag_rt_input_stream(stream_addr);
  if (f) {
    f->eof = 0;
    f->error = 0;
  } else {
    ag_rt_set_errno(9);
  }
}

int __agc_runtime_getchar(void) {
  return __agc_runtime_fgetc((long)&ag_rt_file_value);
}

static int ag_rt_file_read_char(struct ag_rt_file *f) {
  char *src;
  long len;
  int ch;
  if (!f) {
    ag_rt_set_errno(9);
    return -1;
  }
  if (!ag_rt_file_can_read(f)) {
    f->error = 1;
    ag_rt_set_errno(9);
    return -1;
  }
  if (!ag_rt_stream_has_store(f)) {
    f->error = 1;
    ag_rt_set_errno(9);
    return -1;
  }
  if (f->has_ungetc) {
    f->has_ungetc = 0;
    f->eof = 0;
    return f->ungetc_ch;
  }
  src = ag_rt_stream_buf(f);
  len = ag_rt_stream_len(f);
  if (f->pos >= len) {
    f->eof = 1;
    return -1;
  }
  ch = (int)(unsigned char)src[f->pos++];
  ag_rt_file_set_pos(f, f->pos);
  return ch;
}

static int ag_rt_parse_file_mode(long mode_addr, int *write_mode, int *append_mode, int *read_write) {
  char *p;
  if (!mode_addr) return 0;
  char *mode = ag_rt_ptr(mode_addr);
  if (!mode || !mode[0]) return 0;
  if (mode[0] != 'r' && mode[0] != 'w' && mode[0] != 'a') return 0;
  *write_mode = mode[0] == 'w' || mode[0] == 'a';
  *append_mode = mode[0] == 'a';
  *read_write = 0;
  p = mode;
  while (*p) {
    if (*p == '+') *read_write = 1;
    p++;
  }
  return 1;
}

long __agc_runtime_fopen(long path_addr, long mode_addr) {
  int write_mode = 0;
  int append_mode = 0;
  int read_write = 0;
  int store_index;
  struct ag_rt_file *f;
  if (!path_addr) {
    ag_rt_set_errno(22);
    return 0;
  }
  if (!ag_rt_parse_file_mode(mode_addr, &write_mode, &append_mode, &read_write)) {
    ag_rt_set_errno(22);
    return 0;
  }
#ifndef AGC_RUNTIME_JS_CALLBACKS
  if (!ag_rt_store_name_fits(ag_rt_ptr(path_addr))) {
    ag_rt_set_errno(36);
    return 0;
  }
#endif
  if (!ag_rt_has_free_file_slot()) {
    ag_rt_set_errno(12);
    return 0;
  }
#ifdef AGC_RUNTIME_JS_CALLBACKS
  store_index = ag_rt_store_for_path(path_addr, 1);
#else
  store_index = ag_rt_store_for_path(path_addr, write_mode);
#endif
  if (store_index < 0) {
    if (!write_mode && ag_rt_errno_value != 36) ag_rt_set_errno(2);
    return 0;
  }
  if (write_mode && !append_mode) ag_rt_file_stores[store_index].len = 0;
  f = ag_rt_alloc_file(write_mode, append_mode, read_write, -1,
                       append_mode ? ag_rt_file_stores[store_index].len : 0,
                       store_index);
  if (!f) ag_rt_set_errno(12);
  return (long)f;
}

long __agc_runtime_freopen(long path_addr, long mode_addr, long stream_addr) {
  int write_mode = 0;
  int append_mode = 0;
  int read_write = 0;
  int store_index;
  int old_fd_store_index = -1;
  int old_store_index;
  struct ag_rt_file *f;
  if (!path_addr) {
    ag_rt_set_errno(22);
    return 0;
  }
  if (ag_rt_is_stdout_stream(stream_addr) || ag_rt_is_stderr_stream(stream_addr)) {
    ag_rt_set_errno(22);
    return 0;
  }
  if (!ag_rt_parse_file_mode(mode_addr, &write_mode, &append_mode, &read_write)) {
    ag_rt_set_errno(22);
    return 0;
  }
  f = ag_rt_input_stream(stream_addr);
  if (!f) {
    ag_rt_set_errno(9);
    return 0;
  }
#ifdef AGC_RUNTIME_JS_CALLBACKS
  store_index = ag_rt_store_for_path(path_addr, 1);
#else
  store_index = ag_rt_store_for_path(path_addr, write_mode);
#endif
  if (store_index < 0) {
    if (!write_mode && ag_rt_errno_value != 36) ag_rt_set_errno(2);
    return 0;
  }
  old_store_index = f->store_index;
  if (f->fd_index >= 0 && f->fd_index < 8 && ag_rt_fds[f->fd_index].used) {
    old_fd_store_index = ag_rt_fds[f->fd_index].store_index;
    ag_rt_fds[f->fd_index].used = 0;
    ag_rt_fds[f->fd_index].pos = 0;
    ag_rt_fds[f->fd_index].store_index = -1;
    ag_rt_fds[f->fd_index].read_mode = 0;
    ag_rt_fds[f->fd_index].write_mode = 0;
    ag_rt_fds[f->fd_index].append_mode = 0;
  }
  if (write_mode && !append_mode) ag_rt_file_stores[store_index].len = 0;
  ag_rt_file_init(f, write_mode, append_mode, read_write, -1,
                  append_mode ? ag_rt_file_stores[store_index].len : 0, 0,
                  store_index);
#ifndef AGC_RUNTIME_JS_CALLBACKS
  ag_rt_release_temp_store_if_unreferenced(old_fd_store_index);
  ag_rt_release_temp_store_if_unreferenced(old_store_index);
#endif
  if (f == &ag_rt_file_value) __stdinp = (void *)&ag_rt_file_value;
  return (long)f;
}

long __agc_runtime_tmpfile(void) {
  int store_index;
  struct ag_rt_file *f;
  if (!ag_rt_has_free_file_slot()) {
    ag_rt_set_errno(12);
    return 0;
  }
  store_index = ag_rt_temp_store();
  if (store_index < 0) return 0;
  f = ag_rt_alloc_file(1, 0, 1, -1, 0, store_index);
  if (!f) ag_rt_set_errno(12);
  return (long)f;
}

static void ag_rt_tmpnam_write(char *dst, unsigned long seq) {
  char digits[24];
  const char *prefix = "agc_tmp_";
  int i = 0;
  int ndigits = 0;
  while (prefix[i]) {
    dst[i] = prefix[i];
    i++;
  }
  if (seq == 0) {
    digits[ndigits++] = '0';
  } else {
    while (seq > 0 && ndigits < (int)sizeof(digits)) {
      digits[ndigits++] = (char)('0' + (seq % 10));
      seq /= 10;
    }
  }
  while (ndigits > 0) {
    dst[i++] = digits[--ndigits];
  }
  dst[i] = 0;
}

long __agc_runtime_tmpnam(long s_addr) {
  char *dst = s_addr ? ag_rt_ptr(s_addr) : ag_rt_tmpnam_buf;
  unsigned long seq = ag_rt_tmpnam_counter++;
  ag_rt_tmpnam_write(dst, seq);
  return (long)dst;
}

#define AG_RT_O_APPEND 0x0008
#define AG_RT_O_CREAT 0x0200
#define AG_RT_O_TRUNC 0x0400
#define AG_RT_O_EXCL 0x0800
#define AG_RT_O_ACCMODE 0x0003
#define AG_RT_O_WRONLY 0x0001
#define AG_RT_O_RDWR 0x0002

int __agc_runtime_open(long path_addr, int oflag) {
  int store_index;
  int access_mode;
  int read_mode;
  int write_mode;
  if (!path_addr) {
    ag_rt_set_errno(22);
    return -1;
  }
  access_mode = oflag & AG_RT_O_ACCMODE;
  if (access_mode == AG_RT_O_ACCMODE) {
    ag_rt_set_errno(22);
    return -1;
  }
  read_mode = access_mode != AG_RT_O_WRONLY;
  write_mode = access_mode != 0;
#ifndef AGC_RUNTIME_JS_CALLBACKS
  if (!ag_rt_store_name_fits(ag_rt_ptr(path_addr))) {
    ag_rt_set_errno(36);
    return -1;
  }
#endif
  if ((oflag & AG_RT_O_CREAT) && (oflag & AG_RT_O_EXCL) &&
      ag_rt_store_for_path(path_addr, 0) >= 0) {
    ag_rt_set_errno(17);
    return -1;
  }
  if (!ag_rt_has_free_fd_slot()) {
    ag_rt_set_errno(12);
    return -1;
  }
#ifdef AGC_RUNTIME_JS_CALLBACKS
  store_index = ag_rt_store_for_path(path_addr, 1);
#else
  store_index = ag_rt_store_for_path(path_addr, (oflag & AG_RT_O_CREAT) != 0);
#endif
  if (store_index < 0) {
    if (!(oflag & AG_RT_O_CREAT) && ag_rt_errno_value != 36) ag_rt_set_errno(2);
    return -1;
  }
  if ((oflag & AG_RT_O_TRUNC) && write_mode) ag_rt_file_stores[store_index].len = 0;
  for (int i = 0; i < 8; i++) {
    if (!ag_rt_fds[i].used) {
      ag_rt_fds[i].used = 1;
      ag_rt_fds[i].pos = (oflag & AG_RT_O_APPEND) ? ag_rt_file_stores[store_index].len : 0;
      ag_rt_fds[i].store_index = store_index;
      ag_rt_fds[i].read_mode = read_mode;
      ag_rt_fds[i].write_mode = write_mode;
      ag_rt_fds[i].append_mode = (oflag & AG_RT_O_APPEND) != 0;
      return 3 + i;
    }
  }
  ag_rt_set_errno(12);
  return -1;
}

int __agc_runtime_close(int fd) {
  int idx = fd - 3;
  int store_index;
  if (idx < 0 || idx >= 8 || !ag_rt_fds[idx].used) {
    ag_rt_set_errno(9);
    return -1;
  }
  store_index = ag_rt_fds[idx].store_index;
  ag_rt_fds[idx].used = 0;
  ag_rt_fds[idx].pos = 0;
  ag_rt_fds[idx].store_index = -1;
  ag_rt_fds[idx].read_mode = 0;
  ag_rt_fds[idx].write_mode = 0;
  ag_rt_fds[idx].append_mode = 0;
#ifndef AGC_RUNTIME_JS_CALLBACKS
  ag_rt_release_temp_store_if_unreferenced(store_index);
#endif
  return 0;
}

struct ag_rt_stat {
  unsigned short st_mode;
  long st_size;
};

int __agc_runtime_fstat(int fd, long st_addr) {
  int idx = fd - 3;
  if (idx < 0 || idx >= 8 || !ag_rt_fds[idx].used) {
    ag_rt_set_errno(9);
    return -1;
  }
  if (!st_addr) {
    ag_rt_set_errno(22);
    return -1;
  }
  if (ag_rt_fds[idx].store_index < 0 || ag_rt_fds[idx].store_index >= AG_RT_FILE_STORE_COUNT ||
      !ag_rt_file_stores[ag_rt_fds[idx].store_index].used) {
    ag_rt_set_errno(9);
    return -1;
  }
  struct ag_rt_stat *st = (struct ag_rt_stat *)ag_rt_ptr(st_addr);
  st->st_mode = 0100000;
  st->st_size = ag_rt_file_stores[ag_rt_fds[idx].store_index].len;
  return 0;
}

long __agc_runtime_read(int fd, long buf_addr, unsigned long count) {
  int idx = fd - 3;
  long limit;
  long i = 0;
  if (idx < 0 || idx >= 8 || !ag_rt_fds[idx].used) {
    ag_rt_set_errno(9);
    return -1;
  }
  if (!ag_rt_fds[idx].read_mode) {
    ag_rt_set_errno(9);
    return -1;
  }
  limit = (long)count;
  if (limit < 0) {
    ag_rt_set_errno(22);
    return -1;
  }
  char *dst = ag_rt_ptr(buf_addr);
  char *src;
  long len;
  if (ag_rt_fds[idx].store_index < 0 || ag_rt_fds[idx].store_index >= AG_RT_FILE_STORE_COUNT ||
      !ag_rt_file_stores[ag_rt_fds[idx].store_index].used) {
    ag_rt_set_errno(9);
    return -1;
  }
  src = ag_rt_file_stores[ag_rt_fds[idx].store_index].buf;
  len = ag_rt_file_stores[ag_rt_fds[idx].store_index].len;
  while (i < limit && ag_rt_fds[idx].pos < len) {
    dst[i++] = src[ag_rt_fds[idx].pos++];
  }
  return i;
}

long __agc_runtime_write(int fd, long buf_addr, unsigned long count) {
  if (fd == 1 || fd == 2) {
    if (count > 0xffffffffu) {
      ag_rt_set_errno(22);
      return -1;
    }
    char *src = ag_rt_ptr(buf_addr);
    return fd == 1
        ? ag_rt_stdout_write_mem(src, (long)count)
        : ag_rt_stderr_write_mem(src, (long)count);
  }
  int idx = fd - 3;
  long limit;
  long i = 0;
  long gap;
  if (idx < 0 || idx >= 8 || !ag_rt_fds[idx].used) {
    ag_rt_set_errno(9);
    return -1;
  }
  if (!ag_rt_fds[idx].write_mode) {
    ag_rt_set_errno(9);
    return -1;
  }
  limit = (long)count;
  if (limit < 0) {
    ag_rt_set_errno(22);
    return -1;
  }
  char *src = ag_rt_ptr(buf_addr);
  char *dst;
  long *lenp;
  if (ag_rt_fds[idx].store_index < 0 || ag_rt_fds[idx].store_index >= AG_RT_FILE_STORE_COUNT ||
      !ag_rt_file_stores[ag_rt_fds[idx].store_index].used) {
    ag_rt_set_errno(9);
    return -1;
  }
  if (ag_rt_fds[idx].append_mode) {
    ag_rt_fds[idx].pos = ag_rt_file_stores[ag_rt_fds[idx].store_index].len;
  }
  dst = ag_rt_store_buf_for_write(ag_rt_fds[idx].store_index, ag_rt_fds[idx].pos + limit);
  if (!dst) return -1;
  lenp = &ag_rt_file_stores[ag_rt_fds[idx].store_index].len;
  gap = *lenp;
  while (gap < ag_rt_fds[idx].pos && gap < AG_RT_FILE_BUF_CAP) {
    dst[gap++] = 0;
  }
  while (i < limit && ag_rt_fds[idx].pos < AG_RT_FILE_BUF_CAP) {
    dst[ag_rt_fds[idx].pos++] = src[i++];
  }
  if (i > 0 && ag_rt_fds[idx].pos > *lenp) *lenp = ag_rt_fds[idx].pos;
  return i;
}

long __agc_runtime_lseek(int fd, long offset, int whence) {
  int idx = fd - 3;
  long base = 0;
  long next;
  if (idx < 0 || idx >= 8 || !ag_rt_fds[idx].used) {
    ag_rt_set_errno(9);
    return -1;
  }
  if (ag_rt_fds[idx].store_index < 0 || ag_rt_fds[idx].store_index >= AG_RT_FILE_STORE_COUNT ||
      !ag_rt_file_stores[ag_rt_fds[idx].store_index].used) {
    ag_rt_set_errno(9);
    return -1;
  }
  if (whence == 0) {
    base = 0;
  } else if (whence == 1) {
    base = ag_rt_fds[idx].pos;
  } else if (whence == 2) {
    base = ag_rt_file_stores[ag_rt_fds[idx].store_index].len;
  } else {
    ag_rt_set_errno(22);
    return -1;
  }
  next = base + offset;
  if (next < 0) {
    ag_rt_set_errno(22);
    return -1;
  }
  ag_rt_fds[idx].pos = next;
  return next;
}

long __agc_runtime_fdopen(int fd, long mode_addr) {
  int idx = fd - 3;
  int append_mode = 0;
  int write_mode = 0;
  int read_write = 0;
  struct ag_rt_file *f;
  if (idx < 0 || idx >= 8 || !ag_rt_fds[idx].used) {
    ag_rt_set_errno(9);
    return 0;
  }
  if (!ag_rt_parse_file_mode(mode_addr, &write_mode, &append_mode, &read_write)) {
    ag_rt_set_errno(22);
    return 0;
  }
  if (((!write_mode || read_write) && !ag_rt_fds[idx].read_mode) ||
      ((write_mode || append_mode) && !ag_rt_fds[idx].write_mode)) {
    ag_rt_set_errno(9);
    return 0;
  }
  f = ag_rt_alloc_file(write_mode, append_mode, read_write, idx,
                       append_mode ? ag_rt_file_stores[ag_rt_fds[idx].store_index].len : ag_rt_fds[idx].pos,
                       ag_rt_fds[idx].store_index);
  if (!f) ag_rt_set_errno(12);
  return (long)f;
}

int __agc_runtime_fclose(long stream_addr) {
  struct ag_rt_file *f;
  int fd_store_index = -1;
  int store_index;
  if (!stream_addr || stream_addr == (long)__stdinp ||
      ag_rt_is_stdout_stream(stream_addr) || ag_rt_is_stderr_stream(stream_addr)) {
    return 0;
  }
  f = ag_rt_input_stream(stream_addr);
  if (!f) {
    ag_rt_set_errno(9);
    return -1;
  }
  if (f->fd_index >= 0 && f->fd_index < 8 && ag_rt_fds[f->fd_index].used) {
    fd_store_index = ag_rt_fds[f->fd_index].store_index;
    ag_rt_fds[f->fd_index].pos = f->pos;
    ag_rt_fds[f->fd_index].used = 0;
    ag_rt_fds[f->fd_index].store_index = -1;
    ag_rt_fds[f->fd_index].read_mode = 0;
    ag_rt_fds[f->fd_index].write_mode = 0;
    ag_rt_fds[f->fd_index].append_mode = 0;
  }
  store_index = f->store_index;
  f->used = 0;
  f->fd_index = -1;
  f->store_index = -1;
#ifndef AGC_RUNTIME_JS_CALLBACKS
  ag_rt_release_temp_store_if_unreferenced(fd_store_index);
  ag_rt_release_temp_store_if_unreferenced(store_index);
#endif
  return 0;
}

#ifdef AGC_RUNTIME_JS_CALLBACKS
int __agc_runtime_remove(long path_addr) {
  int i;
  if (!path_addr) {
    ag_rt_set_errno(22);
    return -1;
  }
  ag_rt_js_single_store();
  ag_rt_file_stores[0].len = 0;
  for (i = 0; i < 8; i++) {
    if (ag_rt_files[i].used && !ag_rt_files[i].is_stdin) {
      ag_rt_file_set_pos(&ag_rt_files[i], 0);
      ag_rt_files[i].eof = 0;
    }
    if (ag_rt_fds[i].used) ag_rt_fds[i].pos = 0;
  }
  return 0;
}
#else
int __agc_runtime_remove(long path_addr) {
  int store_index;
  if (!path_addr) {
    ag_rt_set_errno(22);
    return -1;
  }
  store_index = ag_rt_store_for_path(path_addr, 0);
  if (store_index < 0) {
    if (ag_rt_errno_value != 36) ag_rt_set_errno(2);
    return -1;
  }
  if (ag_rt_primary_file_store == store_index) ag_rt_primary_file_store = -1;
  ag_rt_file_stores[store_index].used = 0;
  ag_rt_file_stores[store_index].temporary = 0;
  ag_rt_file_stores[store_index].buf = ag_rt_file_stores[store_index].small_buf;
  ag_rt_file_stores[store_index].cap = AG_RT_FILE_SMALL_BUF_CAP;
  ag_rt_file_stores[store_index].len = 0;
  ag_rt_file_stores[store_index].name[0] = 0;
  ag_rt_invalidate_store_refs(store_index);
  return 0;
}
#endif

#ifndef AGC_RUNTIME_JS_CALLBACKS
static void ag_rt_repoint_store_refs(int old_index, int new_index) {
  int i;
  for (i = 0; i < 8; i++) {
    if (ag_rt_files[i].used && !ag_rt_files[i].is_stdin &&
        ag_rt_files[i].store_index == old_index) {
      ag_rt_files[i].store_index = new_index;
    }
    if (ag_rt_fds[i].used && ag_rt_fds[i].store_index == old_index) {
      ag_rt_fds[i].store_index = new_index;
    }
  }
}

static int ag_rt_preserve_replaced_store_refs(int store_index, int exclude_index) {
  int preserve_index;
  long i;
  if (!ag_rt_store_has_refs(store_index)) return 1;
  preserve_index = ag_rt_alloc_temp_store_excluding(store_index, exclude_index);
  if (preserve_index < 0) return 0;
  ag_rt_file_stores[preserve_index].len = ag_rt_file_stores[store_index].len;
  if (ag_rt_primary_file_store == store_index) {
    ag_rt_file_stores[preserve_index].buf = ag_rt_file_buf;
    ag_rt_file_stores[preserve_index].cap = AG_RT_FILE_BUF_CAP;
    ag_rt_primary_file_store = preserve_index;
    ag_rt_file_stores[store_index].buf = ag_rt_file_stores[store_index].small_buf;
    ag_rt_file_stores[store_index].cap = AG_RT_FILE_SMALL_BUF_CAP;
  } else {
    i = 0;
    while (i < ag_rt_file_stores[store_index].len && i < AG_RT_FILE_SMALL_BUF_CAP) {
      ag_rt_file_stores[preserve_index].small_buf[i] = ag_rt_file_stores[store_index].buf[i];
      i++;
    }
    ag_rt_file_stores[preserve_index].buf = ag_rt_file_stores[preserve_index].small_buf;
    ag_rt_file_stores[preserve_index].cap = AG_RT_FILE_SMALL_BUF_CAP;
  }
  ag_rt_repoint_store_refs(store_index, preserve_index);
  return 1;
}
#endif

#ifdef AGC_RUNTIME_JS_CALLBACKS
int __agc_runtime_rename(long oldpath_addr, long newpath_addr) {
  if (!oldpath_addr || !newpath_addr) {
    ag_rt_set_errno(22);
    return -1;
  }
  ag_rt_set_errno(AG_RT_ENOSYS);
  return -1;
}
#else
int __agc_runtime_rename(long oldpath_addr, long newpath_addr) {
  int old_index;
  int new_index;
  long i;
  if (!oldpath_addr || !newpath_addr) {
    ag_rt_set_errno(22);
    return -1;
  }
  old_index = ag_rt_store_for_path(oldpath_addr, 0);
  if (old_index < 0) {
    if (ag_rt_errno_value != 36) ag_rt_set_errno(2);
    return -1;
  }
  if (!ag_rt_store_name_fits(ag_rt_ptr(newpath_addr))) {
    ag_rt_set_errno(36);
    return -1;
  }
  new_index = ag_rt_store_for_path(newpath_addr, 0);
  if (new_index >= 0 && new_index != old_index) {
    if (!ag_rt_preserve_replaced_store_refs(new_index, old_index)) return -1;
    if (ag_rt_primary_file_store == old_index) {
      ag_rt_file_stores[new_index].buf = ag_rt_file_buf;
      ag_rt_file_stores[new_index].cap = AG_RT_FILE_BUF_CAP;
      ag_rt_file_stores[new_index].len = ag_rt_file_stores[old_index].len;
      ag_rt_primary_file_store = new_index;
      ag_rt_repoint_store_refs(old_index, new_index);
      ag_rt_file_stores[old_index].used = 0;
      ag_rt_file_stores[old_index].temporary = 0;
      ag_rt_file_stores[old_index].buf = ag_rt_file_stores[old_index].small_buf;
      ag_rt_file_stores[old_index].cap = AG_RT_FILE_SMALL_BUF_CAP;
      ag_rt_file_stores[old_index].len = 0;
      ag_rt_file_stores[old_index].name[0] = 0;
      return 0;
    }
    ag_rt_file_stores[new_index].len = ag_rt_file_stores[old_index].len;
    if (ag_rt_file_stores[old_index].len <= AG_RT_FILE_SMALL_BUF_CAP) {
      if (ag_rt_primary_file_store == new_index) ag_rt_primary_file_store = -1;
      ag_rt_file_stores[new_index].buf = ag_rt_file_stores[new_index].small_buf;
      ag_rt_file_stores[new_index].cap = AG_RT_FILE_SMALL_BUF_CAP;
      i = 0;
      while (i < ag_rt_file_stores[old_index].len) {
        ag_rt_file_stores[new_index].buf[i] = ag_rt_file_stores[old_index].buf[i];
        i++;
      }
    } else {
      if (!ag_rt_store_buf_for_write(new_index, ag_rt_file_stores[old_index].len)) {
        return -1;
      }
      i = 0;
      while (i < ag_rt_file_stores[old_index].len && i < AG_RT_FILE_BUF_CAP) {
        ag_rt_file_stores[new_index].buf[i] = ag_rt_file_stores[old_index].buf[i];
        i++;
      }
    }
    ag_rt_repoint_store_refs(old_index, new_index);
    ag_rt_file_stores[old_index].used = 0;
    ag_rt_file_stores[old_index].temporary = 0;
    ag_rt_file_stores[old_index].len = 0;
    ag_rt_file_stores[old_index].buf = ag_rt_file_stores[old_index].small_buf;
    ag_rt_file_stores[old_index].cap = AG_RT_FILE_SMALL_BUF_CAP;
    ag_rt_file_stores[old_index].name[0] = 0;
    return 0;
  }
  ag_rt_store_name_copy(ag_rt_file_stores[old_index].name, ag_rt_ptr(newpath_addr));
  return 0;
}
#endif

static int ag_rt_io_total_size(long size, long nmemb, long *total_out) {
  if (size < 0 || nmemb < 0) return 0;
  if (size == 0 || nmemb == 0) {
    *total_out = 0;
    return 1;
  }
  if (size > ag_rt_long_max() / nmemb) return 0;
  *total_out = size * nmemb;
  return 1;
}

long __agc_runtime_fwrite(long ptr_addr, long size, long nmemb, long stream_addr) {
  char *src = ag_rt_ptr(ptr_addr);
  long total = 0;
  struct ag_rt_file *f;
  (void)ag_rt_orient_stream(stream_addr, -1);
  if (!ag_rt_io_total_size(size, nmemb, &total)) {
    ag_rt_set_errno(22);
    return 0;
  }
  if (ag_rt_is_stderr_stream(stream_addr)) {
    long accepted = ag_rt_stderr_write_mem(src, total);
    return size == 0 || accepted < 0 ? 0 : accepted / size;
  } else if (ag_rt_is_stdout_stream(stream_addr)) {
    long accepted = ag_rt_stdout_write_mem(src, total);
    return size == 0 || accepted < 0 ? 0 : accepted / size;
  }
  f = ag_rt_input_stream(stream_addr);
  long i = ag_rt_file_write_mem(f, src, total);
  return size == 0 ? 0 : i / size;
}

long __agc_runtime_fread(long ptr_addr, long size, long nmemb, long stream_addr) {
  struct ag_rt_file *f = ag_rt_input_stream(stream_addr);
  char *dst = ag_rt_ptr(ptr_addr);
  long total = 0;
  long i = 0;
  int ch;
  (void)ag_rt_orient_stream(stream_addr, -1);
  if (!ag_rt_io_total_size(size, nmemb, &total)) {
    ag_rt_set_errno(22);
    return 0;
  }
  if (!f) {
    ag_rt_set_errno(9);
    return 0;
  }
  if (!ag_rt_file_can_read(f)) {
    f->error = 1;
    ag_rt_set_errno(9);
    return 0;
  }
  while (i < total) {
    ch = ag_rt_file_read_char(f);
    if (ch < 0) break;
    dst[i++] = (char)ch;
  }
  return size == 0 ? 0 : i / size;
}

int __agc_runtime_fgetc(long stream_addr) {
  struct ag_rt_file *f = ag_rt_input_stream(stream_addr);
  (void)ag_rt_orient_stream(stream_addr, -1);
  return ag_rt_file_read_char(f);
}

int __agc_runtime_ungetc(int c, long stream_addr) {
  struct ag_rt_file *f = ag_rt_input_stream(stream_addr);
  if (!f) {
    ag_rt_set_errno(9);
    return -1;
  }
  if (!ag_rt_file_can_read(f) || !ag_rt_stream_has_store(f)) {
    f->error = 1;
    ag_rt_set_errno(9);
    return -1;
  }
  if (c == -1 || f->has_ungetc) {
    ag_rt_set_errno(22);
    return -1;
  }
  f->has_ungetc = 1;
  f->ungetc_ch = c & 0xff;
  f->eof = 0;
  return f->ungetc_ch;
}

int __agc_runtime_getc(long stream_addr) {
  return __agc_runtime_fgetc(stream_addr);
}

long __agc_runtime_fgets(long s_addr, int size, long stream_addr) {
  struct ag_rt_file *f = ag_rt_input_stream(stream_addr);
  char *dst = ag_rt_ptr(s_addr);
  int i = 0;
  int ch;
  if (!f) {
    ag_rt_set_errno(9);
    return 0;
  }
  if (!ag_rt_file_can_read(f)) {
    f->error = 1;
    ag_rt_set_errno(9);
    return 0;
  }
  if (size <= 0) {
    ag_rt_set_errno(22);
    return 0;
  }
  while (i + 1 < size) {
    ch = ag_rt_file_read_char(f);
    if (ch < 0) break;
    dst[i++] = (char)ch;
    if (ch == '\n') break;
  }
  if (i == 0) return 0;
  dst[i] = 0;
  return s_addr;
}
