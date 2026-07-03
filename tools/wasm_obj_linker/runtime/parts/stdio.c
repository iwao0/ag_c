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

int __agc_runtime_fputs(long s_addr, long stream_addr) {
  char *s = ag_rt_ptr(s_addr);
  if (ag_rt_is_stderr_stream(stream_addr)) {
    ag_rt_stderr_write_str(s);
  } else if (ag_rt_is_stdout_stream(stream_addr)) {
    ag_rt_stdout_write_str(s);
  }
  return (int)__agc_runtime_strlen(s_addr);
}

int __agc_runtime_fputc(int c, long stream_addr) {
  if (ag_rt_is_stderr_stream(stream_addr)) {
    ag_rt_stderr_write_char(c);
  } else if (ag_rt_is_stdout_stream(stream_addr)) {
    ag_rt_stdout_write_mem((char *)&c, 1);
  }
  return c;
}

int __agc_runtime_fflush(long stream_addr) {
  (void)stream_addr;
  return 0;
}

long __agc_runtime_stdin_capacity(void) {
  return (long)sizeof(ag_rt_file_buf);
}

long __agc_runtime_stdin_write(long ptr_addr, long len) {
  char *src = ag_rt_ptr(ptr_addr);
  long n = len;
  long i = 0;
  if (n < 0) n = 0;
  if (n > (long)sizeof(ag_rt_file_buf)) n = (long)sizeof(ag_rt_file_buf);
  while (i < n) {
    ag_rt_file_buf[i] = src[i];
    i++;
  }
  ag_rt_file_len = n;
  ag_rt_fd_pos = 0;
  ag_rt_file_value.pos = 0;
  ag_rt_file_value.write_mode = 0;
  ag_rt_file_value.eof = 0;
  ag_rt_file_value.error = 0;
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
    base = ag_rt_file_len;
  } else {
    f->error = 1;
    return -1;
  }
  long next = base + offset;
  if (next < 0) {
    f->error = 1;
    return -1;
  }
  f->pos = next;
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
  f->pos = 0;
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

long __agc_runtime_fopen(long path_addr, long mode_addr) {
  (void)path_addr;
  char *mode = ag_rt_ptr(mode_addr);
  ag_rt_file_value.pos = 0;
  ag_rt_file_value.write_mode = mode && mode[0] == 'w';
  ag_rt_file_value.eof = 0;
  ag_rt_file_value.error = 0;
  if (ag_rt_file_value.write_mode) ag_rt_file_len = 0;
  return (long)&ag_rt_file_value;
}

int __agc_runtime_open(long path_addr, int oflag) {
  (void)oflag;
  if (!path_addr) return -1;
  ag_rt_fd_pos = 0;
  return 3;
}

int __agc_runtime_close(int fd) {
  (void)fd;
  return 0;
}

struct ag_rt_stat {
  unsigned short st_mode;
  long st_size;
};

int __agc_runtime_fstat(int fd, long st_addr) {
  (void)fd;
  if (!st_addr) return -1;
  struct ag_rt_stat *st = (struct ag_rt_stat *)ag_rt_ptr(st_addr);
  st->st_mode = 0100000;
  st->st_size = ag_rt_file_len;
  return 0;
}

long __agc_runtime_read(int fd, long buf_addr, unsigned long count) {
  (void)fd;
  char *dst = ag_rt_ptr(buf_addr);
  long limit = (long)count;
  long i = 0;
  while (i < limit && ag_rt_fd_pos < ag_rt_file_len) {
    dst[i++] = ag_rt_file_buf[ag_rt_fd_pos++];
  }
  return i;
}

long __agc_runtime_fdopen(int fd, long mode_addr) {
  (void)fd;
  return __agc_runtime_fopen(0, mode_addr);
}

int __agc_runtime_fclose(long stream_addr) {
  (void)stream_addr;
  return 0;
}

long __agc_runtime_fwrite(long ptr_addr, long size, long nmemb, long stream_addr) {
  char *src = ag_rt_ptr(ptr_addr);
  long total = size * nmemb;
  if (ag_rt_is_stderr_stream(stream_addr)) {
    ag_rt_stderr_write_mem(src, total);
  } else if (ag_rt_is_stdout_stream(stream_addr)) {
    ag_rt_stdout_write_mem(src, total);
  }
  long i = 0;
  while (i < total && ag_rt_file_len < (long)sizeof(ag_rt_file_buf)) {
    ag_rt_file_buf[ag_rt_file_len++] = src[i++];
  }
  return size == 0 ? 0 : i / size;
}

long __agc_runtime_fread(long ptr_addr, long size, long nmemb, long stream_addr) {
  struct ag_rt_file *f = ag_rt_input_stream(stream_addr);
  char *dst = ag_rt_ptr(ptr_addr);
  long total = size * nmemb;
  long i = 0;
  while (i < total && f->pos < ag_rt_file_len) {
    dst[i++] = ag_rt_file_buf[f->pos++];
  }
  if (i < total) f->eof = 1;
  return size == 0 ? 0 : i / size;
}

int __agc_runtime_fgetc(long stream_addr) {
  struct ag_rt_file *f = ag_rt_input_stream(stream_addr);
  if (f->pos >= ag_rt_file_len) {
    f->eof = 1;
    return -1;
  }
  return (int)(unsigned char)ag_rt_file_buf[f->pos++];
}

int __agc_runtime_getc(long stream_addr) {
  return __agc_runtime_fgetc(stream_addr);
}

long __agc_runtime_fgets(long s_addr, int size, long stream_addr) {
  struct ag_rt_file *f = ag_rt_input_stream(stream_addr);
  char *dst = ag_rt_ptr(s_addr);
  int i = 0;
  if (size <= 0) return 0;
  if (f->pos >= ag_rt_file_len) {
    f->eof = 1;
    return 0;
  }
  while (i + 1 < size && f->pos < ag_rt_file_len) {
    char ch = ag_rt_file_buf[f->pos++];
    dst[i++] = ch;
    if (ch == '\n') break;
  }
  dst[i] = 0;
  return s_addr;
}
