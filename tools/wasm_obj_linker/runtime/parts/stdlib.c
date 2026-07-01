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

long __agc_runtime_strtol(long s_addr, long endptr_addr, int base) {
  char *s = ag_rt_ptr(s_addr);
  while (*s == ' ' || *s == '\f' || *s == '\n' || *s == '\r' || *s == '\t' || *s == '\v') s++;
  int sign = 1;
  if (*s == '-') {
    sign = -1;
    s++;
  } else if (*s == '+') {
    s++;
  }
  if (base == 0) base = 10;
  if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
  long acc = 0;
  for (;;) {
    int digit;
    if (*s >= '0' && *s <= '9') digit = *s - '0';
    else if (*s >= 'a' && *s <= 'z') digit = *s - 'a' + 10;
    else if (*s >= 'A' && *s <= 'Z') digit = *s - 'A' + 10;
    else break;
    if (digit >= base) break;
    acc = acc * base + digit;
    s++;
  }
  if (endptr_addr) {
    long *endp = (long *)ag_rt_ptr(endptr_addr);
    *endp = (long)s;
  }
  return sign * acc;
}

unsigned long __agc_runtime_strtoul(long s_addr, long endptr_addr, int base) {
  return __agc_runtime_strtoumax(s_addr, endptr_addr, base);
}

double __agc_runtime_strtod(long s_addr, long endptr_addr) {
  char *s = ag_rt_ptr(s_addr);
  while (*s == ' ' || *s == '\f' || *s == '\n' || *s == '\r' || *s == '\t' || *s == '\v') s++;
  double sign = 1.0;
  if (*s == '-') {
    sign = -1.0;
    s++;
  } else if (*s == '+') {
    s++;
  }

  double acc = 0.0;
  while (*s >= '0' && *s <= '9') {
    acc = acc * 10.0 + (double)(*s - '0');
    s++;
  }
  if (*s == '.') {
    double place = 0.1;
    s++;
    while (*s >= '0' && *s <= '9') {
      acc = acc + (double)(*s - '0') * place;
      place = place / 10.0;
      s++;
    }
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
  char *s = ag_rt_ptr(s_addr);
  while (*s == ' ' || *s == '\f' || *s == '\n' || *s == '\r' || *s == '\t' || *s == '\v') s++;
  int neg = 0;
  if (*s == '-') {
    neg = 1;
    s++;
  } else if (*s == '+') {
    s++;
  }
  if (base == 0) base = 10;
  if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
  unsigned long acc = 0;
  for (;;) {
    int digit;
    if (*s >= '0' && *s <= '9') digit = *s - '0';
    else if (*s >= 'a' && *s <= 'z') digit = *s - 'a' + 10;
    else if (*s >= 'A' && *s <= 'Z') digit = *s - 'A' + 10;
    else break;
    if (digit >= base) break;
    acc = acc * (unsigned long)base + (unsigned long)digit;
    s++;
  }
  if (endptr_addr) {
    long *endp = (long *)ag_rt_ptr(endptr_addr);
    *endp = (long)s;
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

void __agc_runtime_exit(int status) {
  (void)status;
  for (;;) {
  }
}

void __agc_runtime_abort(void) {
  for (;;) {
  }
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
  (void)sig;
  (void)handler_addr;
  return 0;
}

int __agc_runtime_raise(int sig) {
  (void)sig;
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
  (void)timer_addr;
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
  (void)lineptr_addr;
  (void)n_addr;
  (void)stream_addr;
  return -1;
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
