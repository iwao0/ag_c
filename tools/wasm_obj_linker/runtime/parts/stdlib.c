int __agc_runtime_atoi(long s_addr) {
  char *s = ag_rt_ptr(s_addr);
  int sign = 1;
  int acc = 0;
  while (*s == ' ') s++;
  if (*s == '-') {
    sign = -1;
    s++;
  } else if (*s == '+') {
    s++;
  }
  while (*s >= '0' && *s <= '9') {
    acc = acc * 10 + (*s - '0');
    s++;
  }
  return acc * sign;
}

static int __agc_runtime_int_digit(int c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'z') return c - 'a' + 10;
  if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
  return -1;
}

static char *__agc_runtime_int_prefix(char *s, int *base) {
  int b = *base;
  if (b == 0) {
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X') &&
        __agc_runtime_int_digit(s[2]) >= 0 && __agc_runtime_int_digit(s[2]) < 16) {
      *base = 16;
      return s + 2;
    }
    if (s[0] == '0') {
      *base = 8;
      return s;
    }
    *base = 10;
    return s;
  }
  if (b == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X') &&
      __agc_runtime_int_digit(s[2]) >= 0 && __agc_runtime_int_digit(s[2]) < 16) {
    return s + 2;
  }
  return s;
}

long __agc_runtime_strtol(long s_addr, long endptr_addr, int base) {
  char *orig = ag_rt_ptr(s_addr);
  char *s = orig;
  while (*s == ' ' || *s == '\f' || *s == '\n' || *s == '\r' || *s == '\t' || *s == '\v') s++;
  int sign = 1;
  if (*s == '-') {
    sign = -1;
    s++;
  } else if (*s == '+') {
    s++;
  }
  s = __agc_runtime_int_prefix(s, &base);
  char *digits = s;
  long acc = 0;
  for (;;) {
    int digit = __agc_runtime_int_digit(*s);
    if (digit < 0) break;
    if (digit >= base) break;
    acc = acc * base + digit;
    s++;
  }
  if (endptr_addr) {
    long *endp = (long *)ag_rt_ptr(endptr_addr);
    *endp = (long)(s == digits ? orig : s);
  }
  return sign * acc;
}

unsigned long __agc_runtime_strtoul(long s_addr, long endptr_addr, int base) {
  return __agc_runtime_strtoumax(s_addr, endptr_addr, base);
}

static int __agc_runtime_hex_digit(int c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static double __agc_runtime_pow2i(int exp) {
  double scale = 1.0;
  if (exp > 0) {
    while (exp > 0) {
      scale = scale * 2.0;
      exp--;
    }
  } else {
    while (exp < 0) {
      scale = scale / 2.0;
      exp++;
    }
  }
  return scale;
}

static int __agc_runtime_parse_hex_float(char **sp, double *out) {
  char *s = *sp;
  if (!(s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))) return 0;

  char *p = s + 2;
  double acc = 0.0;
  int have_digit = 0;
  int digit;
  while ((digit = __agc_runtime_hex_digit(*p)) >= 0) {
    have_digit = 1;
    acc = acc * 16.0 + (double)digit;
    p++;
  }
  if (*p == '.') {
    double place = 1.0 / 16.0;
    p++;
    while ((digit = __agc_runtime_hex_digit(*p)) >= 0) {
      have_digit = 1;
      acc = acc + (double)digit * place;
      place = place / 16.0;
      p++;
    }
  }
  if (!have_digit || !(*p == 'p' || *p == 'P')) return 0;
  p++;

  int exp_sign = 1;
  if (*p == '-') {
    exp_sign = -1;
    p++;
  } else if (*p == '+') {
    p++;
  }
  int have_exp = 0;
  int exp = 0;
  while (*p >= '0' && *p <= '9') {
    have_exp = 1;
    exp = exp * 10 + (*p - '0');
    p++;
  }
  if (!have_exp) return 0;

  *out = acc * __agc_runtime_pow2i(exp * exp_sign);
  *sp = p;
  return 1;
}

double __agc_runtime_strtod(long s_addr, long endptr_addr) {
  char *orig = ag_rt_ptr(s_addr);
  char *s = orig;
  while (*s == ' ' || *s == '\f' || *s == '\n' || *s == '\r' || *s == '\t' || *s == '\v') s++;
  double sign = 1.0;
  if (*s == '-') {
    sign = -1.0;
    s++;
  } else if (*s == '+') {
    s++;
  }

  double hex = 0.0;
  if (__agc_runtime_parse_hex_float(&s, &hex)) {
    if (endptr_addr) {
      long *endp = (long *)ag_rt_ptr(endptr_addr);
      *endp = (long)s;
    }
    return sign * hex;
  }

  double acc = 0.0;
  int have_digit = 0;
  while (*s >= '0' && *s <= '9') {
    have_digit = 1;
    acc = acc * 10.0 + (double)(*s - '0');
    s++;
  }
  if (*s == '.') {
    double place = 0.1;
    s++;
    while (*s >= '0' && *s <= '9') {
      have_digit = 1;
      acc = acc + (double)(*s - '0') * place;
      place = place / 10.0;
      s++;
    }
  }
  if (!have_digit) {
    if (endptr_addr) {
      long *endp = (long *)ag_rt_ptr(endptr_addr);
      *endp = (long)orig;
    }
    return 0.0;
  }
  if (*s == 'e' || *s == 'E') {
    char *exp_start = s;
    s++;
    int exp_sign = 1;
    if (*s == '-') {
      exp_sign = -1;
      s++;
    } else if (*s == '+') {
      s++;
    }
    int exp = 0;
    int have_exp = 0;
    while (*s >= '0' && *s <= '9') {
      have_exp = 1;
      exp = exp * 10 + (*s - '0');
      s++;
    }
    if (have_exp) {
      while (exp > 0) {
        acc = exp_sign < 0 ? acc / 10.0 : acc * 10.0;
        exp--;
      }
    } else {
      s = exp_start;
    }
  }
  if (endptr_addr) {
    long *endp = (long *)ag_rt_ptr(endptr_addr);
    *endp = (long)s;
  }
  return sign * acc;
}

long __agc_runtime_strtoimax(long s_addr, long endptr_addr, int base) {
  return __agc_runtime_strtol(s_addr, endptr_addr, base);
}

unsigned long __agc_runtime_strtoumax(long s_addr, long endptr_addr, int base) {
  char *orig = ag_rt_ptr(s_addr);
  char *s = orig;
  while (*s == ' ' || *s == '\f' || *s == '\n' || *s == '\r' || *s == '\t' || *s == '\v') s++;
  int neg = 0;
  if (*s == '-') {
    neg = 1;
    s++;
  } else if (*s == '+') {
    s++;
  }
  s = __agc_runtime_int_prefix(s, &base);
  char *digits = s;
  unsigned long acc = 0;
  for (;;) {
    int digit = __agc_runtime_int_digit(*s);
    if (digit < 0) break;
    if (digit >= base) break;
    acc = acc * (unsigned long)base + (unsigned long)digit;
    s++;
  }
  if (endptr_addr) {
    long *endp = (long *)ag_rt_ptr(endptr_addr);
    *endp = (long)(s == digits ? orig : s);
  }
  return neg ? (0UL - acc) : acc;
}

long __agc_runtime_atol(long s_addr) {
  return __agc_runtime_strtol(s_addr, 0, 10);
}

double __agc_runtime_atof(long s_addr) {
  return __agc_runtime_strtod(s_addr, 0);
}

long __agc_runtime_labs(long x) {
  return x < 0 ? -x : x;
}

void __agc_runtime_srand(int seed) {
  ag_rt_rand_state = (unsigned long)seed;
}

int __agc_runtime_rand(void) {
  ag_rt_rand_state = ag_rt_rand_state * 1103515245u + 12345u;
  return (int)((ag_rt_rand_state / 65536u) & 32767u);
}

int __agc_runtime_atexit(long func_addr) {
  (void)func_addr;
  return 0;
}

void __agc_runtime_trap(void);

void __agc_runtime_exit(int status) {
  ag_rt_notify_termination(1, status);
  __agc_runtime_trap();
}

void __agc_runtime_abort(void) {
  ag_rt_notify_termination(2, 0);
  __agc_runtime_trap();
}

long __agc_runtime_getenv(long name_addr) {
  (void)name_addr;
  return 0;
}

long __agc_runtime_realpath(long path_addr, long resolved_path_addr) {
  if (!path_addr) return 0;
  char *path = ag_rt_ptr(path_addr);
  if (!resolved_path_addr) return path_addr;
  char *resolved = ag_rt_ptr(resolved_path_addr);
  long i = 0;
  do {
    resolved[i] = path[i];
  } while (path[i++] != 0);
  return resolved_path_addr;
}

int __agc_runtime_system(long command_addr) {
  (void)command_addr;
  return 0;
}

long __agc_runtime_signal(int sig, long handler_addr) {
  if (sig < 0 || sig >= 32) return 0;
  long old = ag_rt_signal_handlers[sig];
  ag_rt_signal_handlers[sig] = handler_addr;
  return old;
}

int __agc_runtime_raise(int sig) {
  if (sig < 0 || sig >= 32) return -1;
  long handler_addr = ag_rt_signal_handlers[sig];
  if (handler_addr) {
    void (*handler)(int) = (void (*)(int))handler_addr;
    handler(sig);
  }
  return 0;
}

long __agc_runtime_time(long tloc_addr) {
  if (tloc_addr) {
    long *tloc = (long *)ag_rt_ptr(tloc_addr);
    *tloc = 0;
  }
  return 0;
}

long __agc_runtime_clock(void) {
  return 0;
}

double __agc_runtime_difftime(long end, long beginning) {
  return (double)(end - beginning);
}

struct ag_rt_tm {
  int tm_sec;
  int tm_min;
  int tm_hour;
  int tm_mday;
  int tm_mon;
  int tm_year;
  int tm_wday;
  int tm_yday;
  int tm_isdst;
};

static struct ag_rt_tm ag_rt_tm_value;

long __agc_runtime_localtime(long timer_addr) {
  long t = 0;
  if (timer_addr) {
    long *timer = (long *)ag_rt_ptr(timer_addr);
    t = *timer;
  }
  if (t == 0) {
    ag_rt_tm_value.tm_sec = 0;
    ag_rt_tm_value.tm_min = 0;
    ag_rt_tm_value.tm_hour = 0;
    ag_rt_tm_value.tm_mday = 1;
    ag_rt_tm_value.tm_mon = 0;
    ag_rt_tm_value.tm_year = 70;
    ag_rt_tm_value.tm_wday = 4;
    ag_rt_tm_value.tm_yday = 0;
    ag_rt_tm_value.tm_isdst = 0;
  }
  return (long)&ag_rt_tm_value;
}

int __agc_runtime_getrusage(int who, long usage_addr) {
  (void)who;
  if (usage_addr) {
    long *usage = (long *)ag_rt_ptr(usage_addr);
    usage[0] = 0;
  }
  return 0;
}

long __agc_runtime_getline(long lineptr_addr, long n_addr, long stream_addr) {
  char **lineptr = (char **)ag_rt_ptr(lineptr_addr);
  unsigned long *cap = (unsigned long *)ag_rt_ptr(n_addr);
  struct ag_rt_file *f;
  char *src;
  long stream_len;
  long start;
  long len;
  long need;
  long new_cap;
  char *dst;
  char ch;
  long i;
  if (!lineptr || !cap) return -1;
  f = ag_rt_input_stream(stream_addr);
  if (!f) return -1;
  src = ag_rt_stream_buf(f);
  stream_len = ag_rt_stream_len(f);
  if (f->pos >= stream_len) {
    f->eof = 1;
    return -1;
  }
  start = f->pos;
  while (f->pos < stream_len) {
    ch = src[f->pos++];
    if (ch == '\n') break;
  }
  ag_rt_file_set_pos(f, f->pos);
  len = f->pos - start;
  need = len + 1;
  if (!*lineptr || (long)*cap < need) {
    new_cap = *cap ? (long)*cap : 128;
    while (new_cap < need) new_cap *= 2;
    *lineptr = (char *)ag_rt_ptr(__agc_runtime_realloc((long)*lineptr, new_cap));
    *cap = (unsigned long)new_cap;
  }
  dst = *lineptr;
  i = 0;
  while (i < len) {
    dst[i] = src[start + i];
    i++;
  }
  dst[len] = 0;
  return len;
}

int __agc_runtime_setjmp(long env_addr) {
  (void)env_addr;
  return 0;
}

void __agc_runtime_longjmp(long env_addr, int val) {
  (void)env_addr;
  (void)val;
  for (;;) {
  }
}

long __agc_runtime___error(void) {
  return (long)&ag_rt_errno_value;
}
