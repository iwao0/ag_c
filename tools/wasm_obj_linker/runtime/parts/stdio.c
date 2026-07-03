int __agc_runtime_fgetc(long stream_addr);

int __agc_runtime_putchar(int c) {
  ag_rt_stdout_write_mem((char *)&c, 1);
  return c;
}

int __agc_runtime_puts(long s_addr) {
  char *s = ag_rt_ptr(s_addr);
  ag_rt_stdout_write_str(s);
  ag_rt_stdout_write_mem("\n", 1);
  return (int)__agc_runtime_strlen(s_addr) + 1;
}

static long ag_rt_file_write_mem(struct ag_rt_file *f, char *src, long total) {
  long i = 0;
  if (!f || f->is_stdin || total <= 0) return 0;
  while (i < total && f->pos < (long)sizeof(ag_rt_file_buf)) {
    ag_rt_file_buf[f->pos++] = src[i++];
  }
  if (f->pos > ag_rt_file_len) ag_rt_file_len = f->pos;
  ag_rt_file_set_pos(f, f->pos);
  return i;
}

int __agc_runtime_fputs(long s_addr, long stream_addr) {
  char *s = ag_rt_ptr(s_addr);
  long n = __agc_runtime_strlen(s_addr);
  if (ag_rt_is_stderr_stream(stream_addr)) {
    ag_rt_stderr_write_str(s);
  } else if (ag_rt_is_stdout_stream(stream_addr)) {
    ag_rt_stdout_write_str(s);
  } else {
    return ag_rt_file_write_mem(ag_rt_input_stream(stream_addr), s, n) == n ? (int)n : -1;
  }
  return (int)n;
}

int __agc_runtime_fputc(int c, long stream_addr) {
  char ch = (char)c;
  if (ag_rt_is_stderr_stream(stream_addr)) {
    ag_rt_stderr_write_char(c);
  } else if (ag_rt_is_stdout_stream(stream_addr)) {
    ag_rt_stdout_write_mem((char *)&c, 1);
  } else if (ag_rt_file_write_mem(ag_rt_input_stream(stream_addr), &ch, 1) != 1) {
    return -1;
  }
  return c;
}

int __agc_runtime_fflush(long stream_addr) {
  (void)stream_addr;
  return 0;
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
  }
  __stdinp = (void *)&ag_rt_file_value;
  return n;
}

int __agc_runtime_fseek(long stream_addr, long offset, int whence) {
  struct ag_rt_file *f = ag_rt_input_stream(stream_addr);
  long base = 0;
  if (!f) return -1;
  if (whence == 0) {
    base = 0;
  } else if (whence == 1) {
    base = f->pos;
  } else if (whence == 2) {
    base = ag_rt_stream_len(f);
  } else {
    f->error = 1;
    return -1;
  }
  long next = base + offset;
  if (next < 0) {
    f->error = 1;
    return -1;
  }
  ag_rt_file_set_pos(f, next);
  f->eof = 0;
  return 0;
}

long __agc_runtime_ftell(long stream_addr) {
  struct ag_rt_file *f = ag_rt_input_stream(stream_addr);
  if (!f) return -1;
  return f->pos;
}

void __agc_runtime_rewind(long stream_addr) {
  struct ag_rt_file *f = ag_rt_input_stream(stream_addr);
  if (!f) return;
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
  ag_rt_stderr_write_str(ag_rt_strerror);
  ag_rt_stderr_write_char('\n');
}

int __agc_runtime_feof(long stream_addr) {
  struct ag_rt_file *f = ag_rt_input_stream(stream_addr);
  return f ? f->eof : 0;
}

int __agc_runtime_ferror(long stream_addr) {
  struct ag_rt_file *f = ag_rt_input_stream(stream_addr);
  return f ? f->error : 1;
}

void __agc_runtime_clearerr(long stream_addr) {
  struct ag_rt_file *f = ag_rt_input_stream(stream_addr);
  if (f) {
    f->eof = 0;
    f->error = 0;
  }
}

int __agc_runtime_getchar(void) {
  return __agc_runtime_fgetc((long)&ag_rt_file_value);
}

static int ag_rt_parse_file_mode(long mode_addr, int *write_mode, int *append_mode) {
  if (!mode_addr) return 0;
  char *mode = ag_rt_ptr(mode_addr);
  if (!mode || !mode[0]) return 0;
  if (mode[0] != 'r' && mode[0] != 'w' && mode[0] != 'a') return 0;
  *write_mode = mode[0] == 'w' || mode[0] == 'a';
  *append_mode = mode[0] == 'a';
  return 1;
}

long __agc_runtime_fopen(long path_addr, long mode_addr) {
  int write_mode = 0;
  int append_mode = 0;
  struct ag_rt_file *f;
  if (!path_addr) return 0;
  if (!ag_rt_parse_file_mode(mode_addr, &write_mode, &append_mode)) return 0;
  if (write_mode && !append_mode) ag_rt_file_len = 0;
  f = ag_rt_alloc_file(write_mode, -1, append_mode ? ag_rt_file_len : 0);
  return (long)f;
}

#define AG_RT_O_APPEND 0x0008
#define AG_RT_O_TRUNC 0x0400

int __agc_runtime_open(long path_addr, int oflag) {
  if (!path_addr) return -1;
  if (oflag & AG_RT_O_TRUNC) ag_rt_file_len = 0;
  for (int i = 0; i < 8; i++) {
    if (!ag_rt_fds[i].used) {
      ag_rt_fds[i].used = 1;
      ag_rt_fds[i].pos = (oflag & AG_RT_O_APPEND) ? ag_rt_file_len : 0;
      return 3 + i;
    }
  }
  return -1;
}

int __agc_runtime_close(int fd) {
  int idx = fd - 3;
  if (idx < 0 || idx >= 8 || !ag_rt_fds[idx].used) return -1;
  ag_rt_fds[idx].used = 0;
  ag_rt_fds[idx].pos = 0;
  return 0;
}

struct ag_rt_stat {
  unsigned short st_mode;
  long st_size;
};

int __agc_runtime_fstat(int fd, long st_addr) {
  int idx = fd - 3;
  if (idx < 0 || idx >= 8 || !ag_rt_fds[idx].used) return -1;
  if (!st_addr) return -1;
  struct ag_rt_stat *st = (struct ag_rt_stat *)ag_rt_ptr(st_addr);
  st->st_mode = 0100000;
  st->st_size = ag_rt_file_len;
  return 0;
}

long __agc_runtime_read(int fd, long buf_addr, unsigned long count) {
  int idx = fd - 3;
  if (idx < 0 || idx >= 8 || !ag_rt_fds[idx].used) return -1;
  char *dst = ag_rt_ptr(buf_addr);
  long limit = (long)count;
  long i = 0;
  while (i < limit && ag_rt_fds[idx].pos < ag_rt_file_len) {
    dst[i++] = ag_rt_file_buf[ag_rt_fds[idx].pos++];
  }
  return i;
}

long __agc_runtime_write(int fd, long buf_addr, unsigned long count) {
  int idx = fd - 3;
  if (idx < 0 || idx >= 8 || !ag_rt_fds[idx].used) return -1;
  char *src = ag_rt_ptr(buf_addr);
  long limit = (long)count;
  long i = 0;
  while (i < limit && ag_rt_fds[idx].pos < (long)sizeof(ag_rt_file_buf)) {
    ag_rt_file_buf[ag_rt_fds[idx].pos++] = src[i++];
  }
  if (ag_rt_fds[idx].pos > ag_rt_file_len) ag_rt_file_len = ag_rt_fds[idx].pos;
  return i;
}

long __agc_runtime_lseek(int fd, long offset, int whence) {
  int idx = fd - 3;
  long base = 0;
  long next;
  if (idx < 0 || idx >= 8 || !ag_rt_fds[idx].used) return -1;
  if (whence == 0) {
    base = 0;
  } else if (whence == 1) {
    base = ag_rt_fds[idx].pos;
  } else if (whence == 2) {
    base = ag_rt_file_len;
  } else {
    return -1;
  }
  next = base + offset;
  if (next < 0) return -1;
  ag_rt_fds[idx].pos = next;
  return next;
}

long __agc_runtime_fdopen(int fd, long mode_addr) {
  int idx = fd - 3;
  int append_mode = 0;
  int write_mode = 0;
  struct ag_rt_file *f;
  if (idx < 0 || idx >= 8 || !ag_rt_fds[idx].used) return 0;
  if (!ag_rt_parse_file_mode(mode_addr, &write_mode, &append_mode)) return 0;
  f = ag_rt_alloc_file(write_mode, idx, append_mode ? ag_rt_file_len : ag_rt_fds[idx].pos);
  return (long)f;
}

int __agc_runtime_fclose(long stream_addr) {
  struct ag_rt_file *f;
  if (!stream_addr || stream_addr == (long)__stdinp ||
      ag_rt_is_stdout_stream(stream_addr) || ag_rt_is_stderr_stream(stream_addr)) {
    return 0;
  }
  f = (struct ag_rt_file *)ag_rt_ptr(stream_addr);
  if (f) {
    if (f->fd_index >= 0 && f->fd_index < 8 && ag_rt_fds[f->fd_index].used) {
      ag_rt_fds[f->fd_index].pos = f->pos;
      ag_rt_fds[f->fd_index].used = 0;
    }
    f->used = 0;
    f->fd_index = -1;
  }
  return 0;
}

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
  if (!ag_rt_io_total_size(size, nmemb, &total)) return 0;
  if (ag_rt_is_stderr_stream(stream_addr)) {
    ag_rt_stderr_write_mem(src, total);
    return size == 0 ? 0 : nmemb;
  } else if (ag_rt_is_stdout_stream(stream_addr)) {
    ag_rt_stdout_write_mem(src, total);
    return size == 0 ? 0 : nmemb;
  }
  f = ag_rt_input_stream(stream_addr);
  long i = ag_rt_file_write_mem(f, src, total);
  return size == 0 ? 0 : i / size;
}

long __agc_runtime_fread(long ptr_addr, long size, long nmemb, long stream_addr) {
  struct ag_rt_file *f = ag_rt_input_stream(stream_addr);
  char *dst = ag_rt_ptr(ptr_addr);
  long total = 0;
  char *src;
  long len;
  long i = 0;
  if (!ag_rt_io_total_size(size, nmemb, &total)) return 0;
  if (!f) return 0;
  src = ag_rt_stream_buf(f);
  len = ag_rt_stream_len(f);
  while (i < total && f->pos < len) {
    dst[i++] = src[f->pos++];
  }
  ag_rt_file_set_pos(f, f->pos);
  if (i < total) f->eof = 1;
  return size == 0 ? 0 : i / size;
}

int __agc_runtime_fgetc(long stream_addr) {
  struct ag_rt_file *f = ag_rt_input_stream(stream_addr);
  char *src;
  long len;
  if (!f) return -1;
  src = ag_rt_stream_buf(f);
  len = ag_rt_stream_len(f);
  if (f->pos >= len) {
    f->eof = 1;
    return -1;
  }
  int ch = (int)(unsigned char)src[f->pos++];
  ag_rt_file_set_pos(f, f->pos);
  return ch;
}

int __agc_runtime_getc(long stream_addr) {
  return __agc_runtime_fgetc(stream_addr);
}

long __agc_runtime_fgets(long s_addr, int size, long stream_addr) {
  struct ag_rt_file *f = ag_rt_input_stream(stream_addr);
  char *dst = ag_rt_ptr(s_addr);
  char *src;
  long len;
  int i = 0;
  if (!f) return 0;
  src = ag_rt_stream_buf(f);
  len = ag_rt_stream_len(f);
  if (size <= 0) return 0;
  if (f->pos >= len) {
    f->eof = 1;
    return 0;
  }
  while (i + 1 < size && f->pos < len) {
    char ch = src[f->pos++];
    dst[i++] = ch;
    if (ch == '\n') break;
  }
  ag_rt_file_set_pos(f, f->pos);
  dst[i] = 0;
  return s_addr;
}
