#include <stdarg.h>
#include <stddef.h>

static void ag_rt_putc(char *buf, size_t size, int bounded, size_t *pos, int ch) {
  if (!bounded || (long)(*pos + 1) < (long)size) buf[(long)*pos] = (char)ch;
  *pos = *pos + 1;
}

static void ag_rt_finish(char *buf, size_t size, int bounded, size_t pos) {
  if (!bounded) {
    buf[(long)pos] = 0;
  } else if (size != 0) {
    buf[(long)((long)pos < (long)size ? pos : size - 1)] = 0;
  }
}

static void ag_rt_write_str(char *buf, size_t size, int bounded, size_t *pos, const char *s) {
  if (!s) s = "(null)";
  while (*s) {
    ag_rt_putc(buf, size, bounded, pos, *s);
    s++;
  }
}

static char *ag_rt_ptr(long addr) {
  return (char *)addr;
}

static long ag_rt_heap = 8 * 1024 * 1024;
static char ag_rt_locale_c[] = "C";
static char ag_rt_decimal_point[] = ".";
static char ag_rt_strerror[] = "error";
static char ag_rt_file_buf[512];
static long ag_rt_file_len = 0;
static long ag_rt_fd_pos = 0;
static char ag_rt_stdout_buf[8192];
static long ag_rt_stdout_len = 0;
static char ag_rt_stderr_buf[8192];
static long ag_rt_stderr_len = 0;
static long ag_rt_termination_kind = 0;
static long ag_rt_termination_status = 0;
static char *ag_rt_strtok_next;
static unsigned long ag_rt_rand_state = 1;
static int ag_rt_round_mode = 0;
static int ag_rt_errno_value = 0;
void *__stdinp;
void *__stdoutp = (void *)1;
void *__stderrp = (void *)2;

#ifdef AGC_RUNTIME_JS_CALLBACKS
void __agc_runtime_stdout_write(long ptr_addr, long len);
void __agc_runtime_stderr_write(long ptr_addr, long len);
#endif

struct ag_rt_file {
  long pos;
  int write_mode;
  int eof;
  int error;
};

struct ag_rt_lconv {
  char *decimal_point;
};

static struct ag_rt_lconv ag_rt_lconv_value = {ag_rt_decimal_point};
static struct ag_rt_file ag_rt_file_value;

static int ag_rt_is_stdout_stream(long stream_addr) {
  return stream_addr == (long)__stdoutp;
}

static int ag_rt_is_stderr_stream(long stream_addr) {
  return stream_addr == 0 || stream_addr == (long)__stderrp;
}

static void ag_rt_stdout_reset_impl(void) {
  ag_rt_stdout_len = 0;
  ag_rt_stdout_buf[0] = 0;
}

static void ag_rt_stdout_write_mem(const char *s, long n) {
  if (!s || n <= 0) return;
#ifdef AGC_RUNTIME_JS_CALLBACKS
  __agc_runtime_stdout_write((long)s, n);
#endif
  long i = 0;
  while (i < n && ag_rt_stdout_len + 1 < (long)sizeof(ag_rt_stdout_buf)) {
    ag_rt_stdout_buf[ag_rt_stdout_len++] = s[i++];
  }
  ag_rt_stdout_buf[ag_rt_stdout_len] = 0;
}

static void ag_rt_stdout_write_str(const char *s) {
  if (!s) s = "(null)";
  long n = 0;
  while (s[n]) n++;
  ag_rt_stdout_write_mem(s, n);
}

static void ag_rt_stderr_reset_impl(void) {
  ag_rt_stderr_len = 0;
  ag_rt_stderr_buf[0] = 0;
}

static void ag_rt_stderr_write_mem(const char *s, long n) {
  if (!s || n <= 0) return;
#ifdef AGC_RUNTIME_JS_CALLBACKS
  __agc_runtime_stderr_write((long)s, n);
#endif
  long i = 0;
  while (i < n && ag_rt_stderr_len + 1 < (long)sizeof(ag_rt_stderr_buf)) {
    ag_rt_stderr_buf[ag_rt_stderr_len++] = s[i++];
  }
  ag_rt_stderr_buf[ag_rt_stderr_len] = 0;
}

static void ag_rt_stderr_write_str(const char *s) {
  if (!s) s = "(null)";
  long n = 0;
  while (s[n]) n++;
  ag_rt_stderr_write_mem(s, n);
}

static void ag_rt_stderr_write_char(int ch) {
  char c = (char)ch;
  ag_rt_stderr_write_mem(&c, 1);
}

long __agc_runtime_stdout_ptr(void) {
  return (long)ag_rt_stdout_buf;
}

long __agc_runtime_stdout_len(void) {
  return ag_rt_stdout_len;
}

long __agc_runtime_stderr_ptr(void) {
  return (long)ag_rt_stderr_buf;
}

long __agc_runtime_stderr_len(void) {
  return ag_rt_stderr_len;
}

void __agc_runtime_stderr_reset(void) {
  ag_rt_stdout_reset_impl();
  ag_rt_stderr_reset_impl();
  ag_rt_termination_kind = 0;
  ag_rt_termination_status = 0;
}

static void ag_rt_notify_termination(int kind, int status) {
  ag_rt_termination_kind = kind;
  ag_rt_termination_status = status;
}

long __agc_runtime_termination_kind(void) {
  return ag_rt_termination_kind;
}

long __agc_runtime_termination_status(void) {
  return ag_rt_termination_status;
}

double __agc_runtime_exp(double x);
double __agc_runtime_log(double x);
long __agc_runtime_memcpy(long dst_addr, long src_addr, long n);
long __agc_runtime_wcstol(long nptr_addr, long endptr_addr, int base);
unsigned long __agc_runtime_strtoumax(long s_addr, long endptr_addr, int base);
int __agc_runtime_strcmp(long a_addr, long b_addr);
