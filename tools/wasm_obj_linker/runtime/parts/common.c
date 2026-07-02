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

static long ag_rt_heap = 524288;
static char ag_rt_locale_c[] = "C";
static char ag_rt_decimal_point[] = ".";
static char ag_rt_strerror[] = "error";
static char ag_rt_file_buf[512];
static long ag_rt_file_len = 0;
static long ag_rt_fd_pos = 0;
static char *ag_rt_strtok_next;
static unsigned long ag_rt_rand_state = 1;
static int ag_rt_round_mode = 0;
static int ag_rt_errno_value = 0;
void *__stdinp;
void *__stdoutp;
void *__stderrp;

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

double __agc_runtime_exp(double x);
double __agc_runtime_log(double x);
long __agc_runtime_memcpy(long dst_addr, long src_addr, long n);
long __agc_runtime_wcstol(long nptr_addr, long endptr_addr, int base);
unsigned long __agc_runtime_strtoumax(long s_addr, long endptr_addr, int base);
int __agc_runtime_strcmp(long a_addr, long b_addr);
