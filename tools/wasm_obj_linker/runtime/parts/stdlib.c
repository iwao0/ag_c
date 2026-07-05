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

static int __agc_runtime_int_base_ok(int base) {
  return base == 0 || (base >= 2 && base <= 36);
}

static void __agc_runtime_set_errno(int value) {
  ag_rt_set_errno(value);
}

long __agc_runtime_strtol(long s_addr, long endptr_addr, int base) {
  char *orig = ag_rt_ptr(s_addr);
  char *s = orig;
  if (!__agc_runtime_int_base_ok(base)) {
    __agc_runtime_set_errno(22);
    if (endptr_addr) {
      long *endp = (long *)ag_rt_ptr(endptr_addr);
      *endp = (long)orig;
    }
    return 0;
  }
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
  unsigned long limit = sign < 0 ? (1UL << 63) : ((1UL << 63) - 1UL);
  unsigned long cutoff = limit / (unsigned long)base;
  unsigned long cutlim = limit % (unsigned long)base;
  unsigned long acc = 0;
  int overflow = 0;
  for (;;) {
    int digit = __agc_runtime_int_digit(*s);
    if (digit < 0) break;
    if (digit >= base) break;
    if (acc > cutoff || (acc == cutoff && (unsigned long)digit > cutlim)) {
      overflow = 1;
    } else {
      acc = acc * (unsigned long)base + (unsigned long)digit;
    }
    s++;
  }
  if (endptr_addr) {
    long *endp = (long *)ag_rt_ptr(endptr_addr);
    *endp = (long)(s == digits ? orig : s);
  }
  if (s == digits) return 0;
  if (overflow) {
    __agc_runtime_set_errno(34);
    return sign < 0 ? (long)(1UL << 63) : (long)((1UL << 63) - 1UL);
  }
  if (sign < 0) {
    if (acc == (1UL << 63)) return (long)(1UL << 63);
    return -(long)acc;
  }
  return (long)acc;
}

unsigned long __agc_runtime_strtoul(long s_addr, long endptr_addr, int base) {
  return __agc_runtime_strtoumax(s_addr, endptr_addr, base);
}

long long __agc_runtime_strtoll(long s_addr, long endptr_addr, int base) {
  return (long long)__agc_runtime_strtol(s_addr, endptr_addr, base);
}

unsigned long long __agc_runtime_strtoull(long s_addr, long endptr_addr, int base) {
  return (unsigned long long)__agc_runtime_strtoumax(s_addr, endptr_addr, base);
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

static double __agc_runtime_float_inf(void) {
  double zero = 0.0;
  return 1.0 / zero;
}

static double __agc_runtime_float_nan(void) {
  double zero = 0.0;
  return zero / zero;
}

static double __agc_runtime_double_max(void) {
  return 1.7976931348623157e308;
}

static int __agc_runtime_double_is_inf(double v) {
  double inf = __agc_runtime_float_inf();
  return v == inf || v == -inf;
}

static double __agc_runtime_finish_strtod(double value, double sign, int nonzero, int range_dir) {
  double max = __agc_runtime_double_max();
  if (range_dir > 0 || value > max || __agc_runtime_double_is_inf(value)) {
    __agc_runtime_set_errno(34);
    return sign < 0.0 ? -__agc_runtime_float_inf() : __agc_runtime_float_inf();
  }
  if ((range_dir < 0 || value == 0.0) && nonzero) {
    __agc_runtime_set_errno(34);
    return sign < 0.0 ? -0.0 : 0.0;
  }
  return sign * value;
}

static int __agc_runtime_strtod_ascii_lower(int ch) {
  if (ch >= 'A' && ch <= 'Z') return ch - 'A' + 'a';
  return ch;
}

static int __agc_runtime_strtod_match_lit(char *p, char *lit) {
  int n = 0;
  while (lit[n]) {
    if (__agc_runtime_strtod_ascii_lower((unsigned char)p[n]) != lit[n]) return 0;
    n++;
  }
  return n;
}

static int __agc_runtime_strtod_nan_payload_len(char *p) {
  int n = 0;
  if (*p != '(') return 0;
  p++;
  n++;
  while (*p && *p != ')') {
    p++;
    n++;
  }
  if (*p == ')') return n + 1;
  return 0;
}

static int __agc_runtime_parse_special_float(char **sp, double sign, double *out) {
  char *p = *sp;
  int special_len = __agc_runtime_strtod_match_lit(p, "infinity");
  if (special_len == 0) special_len = __agc_runtime_strtod_match_lit(p, "inf");
  if (special_len != 0) {
    *sp = p + special_len;
    *out = sign < 0.0 ? -__agc_runtime_float_inf() : __agc_runtime_float_inf();
    return 1;
  }
  special_len = __agc_runtime_strtod_match_lit(p, "nan");
  if (special_len != 0) {
    int payload_len = __agc_runtime_strtod_nan_payload_len(p + special_len);
    *sp = p + special_len + payload_len;
    *out = __agc_runtime_float_nan();
    return 1;
  }
  return 0;
}

static int __agc_runtime_parse_hex_float(char **sp, double *out, int *nonzero) {
  char *s = *sp;
  if (!(s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))) return 0;

  char *p = s + 2;
  double acc = 0.0;
  int have_digit = 0;
  int digit;
  while ((digit = __agc_runtime_hex_digit(*p)) >= 0) {
    have_digit = 1;
    if (digit != 0) *nonzero = 1;
    acc = acc * 16.0 + (double)digit;
    p++;
  }
  if (ag_rt_is_decimal_point((unsigned char)*p)) {
    double place = 1.0 / 16.0;
    p++;
    while ((digit = __agc_runtime_hex_digit(*p)) >= 0) {
      have_digit = 1;
      if (digit != 0) *nonzero = 1;
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
  int exp_overflow = 0;
  while (*p >= '0' && *p <= '9') {
    have_exp = 1;
    if (exp < 4096) {
      exp = exp * 10 + (*p - '0');
    } else {
      exp_overflow = 1;
    }
    p++;
  }
  if (!have_exp) return 0;

  if (exp_overflow && *nonzero) {
    *out = exp_sign < 0 ? 0.0 : __agc_runtime_float_inf();
  } else {
    *out = acc * __agc_runtime_pow2i(exp * exp_sign);
  }
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

  double special = 0.0;
  if (__agc_runtime_parse_special_float(&s, sign, &special)) {
    if (endptr_addr) {
      long *endp = (long *)ag_rt_ptr(endptr_addr);
      *endp = (long)s;
    }
    return special;
  }

  double hex = 0.0;
  int nonzero = 0;
  if (__agc_runtime_parse_hex_float(&s, &hex, &nonzero)) {
    if (endptr_addr) {
      long *endp = (long *)ag_rt_ptr(endptr_addr);
      *endp = (long)s;
    }
    return __agc_runtime_finish_strtod(hex, sign, nonzero, 0);
  }

  double acc = 0.0;
  int have_digit = 0;
  int range_dir = 0;
  double max = __agc_runtime_double_max();
  while (*s >= '0' && *s <= '9') {
    have_digit = 1;
    if (*s != '0') nonzero = 1;
    if (acc > max / 10.0) {
      range_dir = 1;
      acc = __agc_runtime_float_inf();
    } else if (!__agc_runtime_double_is_inf(acc)) {
      acc = acc * 10.0 + (double)(*s - '0');
      if (acc > max) range_dir = 1;
    }
    s++;
  }
  if (ag_rt_is_decimal_point((unsigned char)*s)) {
    double place = 0.1;
    s++;
    while (*s >= '0' && *s <= '9') {
      have_digit = 1;
      if (*s != '0') nonzero = 1;
      if (!__agc_runtime_double_is_inf(acc)) {
        acc = acc + (double)(*s - '0') * place;
      }
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
    int exp_overflow = 0;
    while (*s >= '0' && *s <= '9') {
      have_exp = 1;
      if (exp < 4096) {
        exp = exp * 10 + (*s - '0');
      } else {
        exp_overflow = 1;
      }
      s++;
    }
    if (have_exp) {
      if (exp_overflow && nonzero) {
        range_dir = exp_sign < 0 ? -1 : 1;
        acc = exp_sign < 0 ? 0.0 : __agc_runtime_float_inf();
      } else {
        while (exp > 0) {
          if (exp_sign < 0) {
            acc = acc / 10.0;
          } else if (acc > max / 10.0) {
            range_dir = 1;
            acc = __agc_runtime_float_inf();
          } else {
            acc = acc * 10.0;
          }
          exp--;
        }
      }
    } else {
      s = exp_start;
    }
  }
  if (endptr_addr) {
    long *endp = (long *)ag_rt_ptr(endptr_addr);
    *endp = (long)s;
  }
  return __agc_runtime_finish_strtod(acc, sign, nonzero, range_dir);
}

float __agc_runtime_strtof(long s_addr, long endptr_addr) {
  return (float)__agc_runtime_strtod(s_addr, endptr_addr);
}

long double __agc_runtime_strtold(long s_addr, long endptr_addr) {
  return (long double)__agc_runtime_strtod(s_addr, endptr_addr);
}

long __agc_runtime_strtoimax(long s_addr, long endptr_addr, int base) {
  return __agc_runtime_strtol(s_addr, endptr_addr, base);
}

unsigned long __agc_runtime_strtoumax(long s_addr, long endptr_addr, int base) {
  char *orig = ag_rt_ptr(s_addr);
  char *s = orig;
  if (!__agc_runtime_int_base_ok(base)) {
    __agc_runtime_set_errno(22);
    if (endptr_addr) {
      long *endp = (long *)ag_rt_ptr(endptr_addr);
      *endp = (long)orig;
    }
    return 0;
  }
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
  unsigned long cutoff = ~0UL / (unsigned long)base;
  unsigned long cutlim = ~0UL % (unsigned long)base;
  int overflow = 0;
  for (;;) {
    int digit = __agc_runtime_int_digit(*s);
    if (digit < 0) break;
    if (digit >= base) break;
    if (acc > cutoff || (acc == cutoff && (unsigned long)digit > cutlim)) {
      overflow = 1;
    } else {
      acc = acc * (unsigned long)base + (unsigned long)digit;
    }
    s++;
  }
  if (endptr_addr) {
    long *endp = (long *)ag_rt_ptr(endptr_addr);
    *endp = (long)(s == digits ? orig : s);
  }
  if (s == digits) return 0;
  if (overflow) {
    __agc_runtime_set_errno(34);
    return ~0UL;
  }
  return neg ? (0UL - acc) : acc;
}

long __agc_runtime_atol(long s_addr) {
  return __agc_runtime_strtol(s_addr, 0, 10);
}

long long __agc_runtime_atoll(long s_addr) {
  return __agc_runtime_strtoll(s_addr, 0, 10);
}

double __agc_runtime_atof(long s_addr) {
  return __agc_runtime_strtod(s_addr, 0);
}

long __agc_runtime_labs(long x) {
  return x < 0 ? -x : x;
}

long long __agc_runtime_llabs(long long x) {
  return x < 0 ? -x : x;
}

typedef struct {
  int quot;
  int rem;
} ag_rt_div_t;

typedef struct {
  long quot;
  long rem;
} ag_rt_ldiv_t;

typedef struct {
  long long quot;
  long long rem;
} ag_rt_lldiv_t;

ag_rt_div_t __agc_runtime_div(int numer, int denom) {
  ag_rt_div_t r = {0, 0};
  r.quot = numer / denom;
  r.rem = numer - r.quot * denom;
  return r;
}

ag_rt_ldiv_t __agc_runtime_ldiv(long numer, long denom) {
  ag_rt_ldiv_t r = {0, 0};
  r.quot = numer / denom;
  r.rem = numer - r.quot * denom;
  return r;
}

ag_rt_lldiv_t __agc_runtime_lldiv(long long numer, long long denom) {
  ag_rt_lldiv_t r = {0, 0};
  r.quot = numer / denom;
  r.rem = numer - r.quot * denom;
  return r;
}

ag_rt_lldiv_t __agc_runtime_imaxdiv(long long numer, long long denom) {
  return __agc_runtime_lldiv(numer, denom);
}

void __agc_runtime_srand(int seed) {
  ag_rt_rand_state = (unsigned long)seed;
}

int __agc_runtime_rand(void) {
  ag_rt_rand_state = ag_rt_rand_state * 1103515245u + 12345u;
  return (int)((ag_rt_rand_state / 65536u) & 32767u);
}

int __agc_runtime_atexit(long func_addr) {
  if (!func_addr) return 0;
  if (ag_rt_atexit_count >= 32) return -1;
  ag_rt_atexit_handlers[ag_rt_atexit_count++] = func_addr;
  return 0;
}

int __agc_runtime_at_quick_exit(long func_addr) {
  if (!func_addr) return 0;
  if (ag_rt_quick_exit_count >= 32) return -1;
  ag_rt_quick_exit_handlers[ag_rt_quick_exit_count++] = func_addr;
  return 0;
}

static void ag_rt_run_atexit_handlers(void) {
  while (ag_rt_atexit_count > 0) {
    long func_addr = ag_rt_atexit_handlers[--ag_rt_atexit_count];
    ag_rt_atexit_handlers[ag_rt_atexit_count] = 0;
    if (func_addr) {
      void (*func)(void) = (void (*)(void))func_addr;
      func();
    }
  }
}

static void ag_rt_run_quick_exit_handlers(void) {
  while (ag_rt_quick_exit_count > 0) {
    long func_addr = ag_rt_quick_exit_handlers[--ag_rt_quick_exit_count];
    ag_rt_quick_exit_handlers[ag_rt_quick_exit_count] = 0;
    if (func_addr) {
      void (*func)(void) = (void (*)(void))func_addr;
      func();
    }
  }
}

void __agc_runtime_trap(void);

void __agc_runtime_exit(int status) {
  ag_rt_run_atexit_handlers();
  ag_rt_notify_termination(1, status);
  __agc_runtime_trap();
}

void __agc_runtime_quick_exit(int status) {
  ag_rt_run_quick_exit_handlers();
  ag_rt_notify_termination(3, status);
  __agc_runtime_trap();
}

void __agc_runtime__Exit(int status) {
  ag_rt_notify_termination(4, status);
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
  char *resolved;
  if (resolved_path_addr) {
    resolved = ag_rt_ptr(resolved_path_addr);
  } else {
    long n = 0;
    while (path[n]) n++;
    resolved_path_addr = __agc_runtime_malloc(n + 1);
    resolved = ag_rt_ptr(resolved_path_addr);
  }
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
  if (sig < 0 || sig >= 32) return -1;
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

struct ag_rt_timespec {
  long tv_sec;
  long tv_nsec;
};

int __agc_runtime_timespec_get(long ts_addr, int base) {
  struct ag_rt_timespec *ts = (struct ag_rt_timespec *)ag_rt_ptr(ts_addr);
  if (!ts || base != 1) return 0;
  ts->tv_sec = 0;
  ts->tv_nsec = 0;
  return base;
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
static char ag_rt_asctime_buf[64];
static char ag_rt_ctime_buf[64];

static int ag_rt_time_is_leap(int year) {
  if (year % 400 == 0) return 1;
  if (year % 100 == 0) return 0;
  return year % 4 == 0;
}

static int ag_rt_time_days_in_year(int year) {
  return ag_rt_time_is_leap(year) ? 366 : 365;
}

static int ag_rt_time_days_in_month(int year, int month) {
  if (month == 1) return ag_rt_time_is_leap(year) ? 29 : 28;
  if (month == 3 || month == 5 || month == 8 || month == 10) return 30;
  return 31;
}

static long ag_rt_time_days_before_month(int year, int month) {
  long days = 0;
  int m = 0;
  while (m < month) {
    days = days + ag_rt_time_days_in_month(year, m);
    m++;
  }
  return days;
}

static char *ag_rt_time_wday_name(int wday) {
  if (wday == 0) return "Sun";
  if (wday == 1) return "Mon";
  if (wday == 2) return "Tue";
  if (wday == 3) return "Wed";
  if (wday == 4) return "Thu";
  if (wday == 5) return "Fri";
  return "Sat";
}

static char *ag_rt_time_wday_full_name(int wday) {
  if (wday == 0) return "Sunday";
  if (wday == 1) return "Monday";
  if (wday == 2) return "Tuesday";
  if (wday == 3) return "Wednesday";
  if (wday == 4) return "Thursday";
  if (wday == 5) return "Friday";
  return "Saturday";
}

static char *ag_rt_time_mon_name(int mon) {
  if (mon == 0) return "Jan";
  if (mon == 1) return "Feb";
  if (mon == 2) return "Mar";
  if (mon == 3) return "Apr";
  if (mon == 4) return "May";
  if (mon == 5) return "Jun";
  if (mon == 6) return "Jul";
  if (mon == 7) return "Aug";
  if (mon == 8) return "Sep";
  if (mon == 9) return "Oct";
  if (mon == 10) return "Nov";
  return "Dec";
}

static char *ag_rt_time_mon_full_name(int mon) {
  if (mon == 0) return "January";
  if (mon == 1) return "February";
  if (mon == 2) return "March";
  if (mon == 3) return "April";
  if (mon == 4) return "May";
  if (mon == 5) return "June";
  if (mon == 6) return "July";
  if (mon == 7) return "August";
  if (mon == 8) return "September";
  if (mon == 9) return "October";
  if (mon == 10) return "November";
  return "December";
}

static void ag_rt_time_from_seconds(long t, struct ag_rt_tm *out) {
  long days;
  long rem;
  int year;
  int mon;
  int yday;
  if (!out) return;
  if (t < 0) t = 0;
  days = t / 86400;
  rem = t % 86400;
  out->tm_hour = (int)(rem / 3600);
  rem = rem % 3600;
  out->tm_min = (int)(rem / 60);
  out->tm_sec = (int)(rem % 60);
  out->tm_wday = (int)((4 + days) % 7);
  year = 1970;
  while (days >= ag_rt_time_days_in_year(year)) {
    days = days - ag_rt_time_days_in_year(year);
    year++;
  }
  yday = (int)days;
  mon = 0;
  while (days >= ag_rt_time_days_in_month(year, mon)) {
    days = days - ag_rt_time_days_in_month(year, mon);
    mon++;
  }
  out->tm_mday = (int)days + 1;
  out->tm_mon = mon;
  out->tm_year = year - 1900;
  out->tm_yday = yday;
  out->tm_isdst = 0;
}

static long ag_rt_time_to_seconds(struct ag_rt_tm *tm) {
  int year;
  int mon;
  long days = 0;
  if (!tm) return -1;
  year = tm->tm_year + 1900;
  mon = tm->tm_mon;
  while (mon < 0) {
    mon = mon + 12;
    year--;
  }
  while (mon >= 12) {
    mon = mon - 12;
    year++;
  }
  for (int y = 1970; y < year; y++) days = days + ag_rt_time_days_in_year(y);
  days = days + ag_rt_time_days_before_month(year, mon) + tm->tm_mday - 1;
  return days * 86400 + (long)tm->tm_hour * 3600 + (long)tm->tm_min * 60 + tm->tm_sec;
}

static void ag_rt_time_append_char(char *buf, long *pos, int ch) {
  buf[*pos] = (char)ch;
  *pos = *pos + 1;
}

static void ag_rt_time_append_str(char *buf, long *pos, char *s) {
  while (*s) {
    ag_rt_time_append_char(buf, pos, *s);
    s++;
  }
}

static void ag_rt_time_append_num(char *buf, long *pos, long value, int width, int zero_pad) {
  char tmp[32];
  long n = 0;
  long i;
  int negative = 0;
  if (value < 0) {
    negative = 1;
    value = -value;
  }
  do {
    tmp[n++] = (char)('0' + value % 10);
    value = value / 10;
  } while (value != 0 && n < 31);
  if (negative) width--;
  while (n < width) {
    ag_rt_time_append_char(buf, pos, zero_pad ? '0' : ' ');
    width--;
  }
  if (negative) ag_rt_time_append_char(buf, pos, '-');
  for (i = n - 1; i >= 0; i--) ag_rt_time_append_char(buf, pos, tmp[i]);
}

static char *ag_rt_asctime_impl(struct ag_rt_tm *tm, char *buf) {
  long pos = 0;
  if (!tm) return 0;
  ag_rt_time_append_str(buf, &pos, ag_rt_time_wday_name(tm->tm_wday));
  ag_rt_time_append_char(buf, &pos, ' ');
  ag_rt_time_append_str(buf, &pos, ag_rt_time_mon_name(tm->tm_mon));
  ag_rt_time_append_char(buf, &pos, ' ');
  ag_rt_time_append_num(buf, &pos, tm->tm_mday, 2, 0);
  ag_rt_time_append_char(buf, &pos, ' ');
  ag_rt_time_append_num(buf, &pos, tm->tm_hour, 2, 1);
  ag_rt_time_append_char(buf, &pos, ':');
  ag_rt_time_append_num(buf, &pos, tm->tm_min, 2, 1);
  ag_rt_time_append_char(buf, &pos, ':');
  ag_rt_time_append_num(buf, &pos, tm->tm_sec, 2, 1);
  ag_rt_time_append_char(buf, &pos, ' ');
  ag_rt_time_append_num(buf, &pos, tm->tm_year + 1900, 4, 0);
  ag_rt_time_append_char(buf, &pos, '\n');
  ag_rt_time_append_char(buf, &pos, 0);
  return buf;
}

long __agc_runtime_gmtime(long timer_addr) {
  long t = 0;
  if (timer_addr) {
    long *timer = (long *)ag_rt_ptr(timer_addr);
    t = *timer;
  }
  ag_rt_time_from_seconds(t, &ag_rt_tm_value);
  return (long)&ag_rt_tm_value;
}

long __agc_runtime_localtime(long timer_addr) {
  return __agc_runtime_gmtime(timer_addr);
}

long __agc_runtime_mktime(long timeptr_addr) {
  struct ag_rt_tm *tm = (struct ag_rt_tm *)ag_rt_ptr(timeptr_addr);
  long t = ag_rt_time_to_seconds(tm);
  if (tm && t >= 0) ag_rt_time_from_seconds(t, tm);
  return t;
}

long __agc_runtime_asctime(long timeptr_addr) {
  struct ag_rt_tm *tm = (struct ag_rt_tm *)ag_rt_ptr(timeptr_addr);
  return (long)ag_rt_asctime_impl(tm, ag_rt_asctime_buf);
}

long __agc_runtime_ctime(long timer_addr) {
  long tm_addr = __agc_runtime_localtime(timer_addr);
  return (long)ag_rt_asctime_impl((struct ag_rt_tm *)ag_rt_ptr(tm_addr), ag_rt_ctime_buf);
}

static int ag_rt_strftime_put_char(char *dst, long maxsize, long *pos, int ch) {
  if (*pos + 1 >= maxsize) return 0;
  dst[*pos] = (char)ch;
  *pos = *pos + 1;
  return 1;
}

static int ag_rt_strftime_put_str(char *dst, long maxsize, long *pos, char *s) {
  while (*s) {
    if (!ag_rt_strftime_put_char(dst, maxsize, pos, *s)) return 0;
    s++;
  }
  return 1;
}

static int ag_rt_strftime_put_num_pad(char *dst, long maxsize, long *pos, long value, int width, int pad_ch) {
  char tmp[32];
  long n = 0;
  long i;
  do {
    tmp[n++] = (char)('0' + value % 10);
    value = value / 10;
  } while (value != 0 && n < 31);
  while (n < width) {
    if (!ag_rt_strftime_put_char(dst, maxsize, pos, pad_ch)) return 0;
    width--;
  }
  for (i = n - 1; i >= 0; i--) {
    if (!ag_rt_strftime_put_char(dst, maxsize, pos, tmp[i])) return 0;
  }
  return 1;
}

static int ag_rt_strftime_put_num(char *dst, long maxsize, long *pos, long value, int width) {
  return ag_rt_strftime_put_num_pad(dst, maxsize, pos, value, width, '0');
}

static int ag_rt_strftime_put_format(char *dst, long maxsize, long *pos, int spec, struct ag_rt_tm *tm) {
  if (spec == '%') return ag_rt_strftime_put_char(dst, maxsize, pos, '%');
  if (spec == 'a') return ag_rt_strftime_put_str(dst, maxsize, pos, ag_rt_time_wday_name(tm->tm_wday));
  if (spec == 'A') return ag_rt_strftime_put_str(dst, maxsize, pos, ag_rt_time_wday_full_name(tm->tm_wday));
  if (spec == 'b' || spec == 'h') return ag_rt_strftime_put_str(dst, maxsize, pos, ag_rt_time_mon_name(tm->tm_mon));
  if (spec == 'B') return ag_rt_strftime_put_str(dst, maxsize, pos, ag_rt_time_mon_full_name(tm->tm_mon));
  if (spec == 'd') return ag_rt_strftime_put_num(dst, maxsize, pos, tm->tm_mday, 2);
  if (spec == 'e') return ag_rt_strftime_put_num_pad(dst, maxsize, pos, tm->tm_mday, 2, ' ');
  if (spec == 'H') return ag_rt_strftime_put_num(dst, maxsize, pos, tm->tm_hour, 2);
  if (spec == 'j') return ag_rt_strftime_put_num(dst, maxsize, pos, tm->tm_yday + 1, 3);
  if (spec == 'm') return ag_rt_strftime_put_num(dst, maxsize, pos, tm->tm_mon + 1, 2);
  if (spec == 'M') return ag_rt_strftime_put_num(dst, maxsize, pos, tm->tm_min, 2);
  if (spec == 'S') return ag_rt_strftime_put_num(dst, maxsize, pos, tm->tm_sec, 2);
  if (spec == 'w') return ag_rt_strftime_put_num(dst, maxsize, pos, tm->tm_wday, 1);
  if (spec == 'y') return ag_rt_strftime_put_num(dst, maxsize, pos, (tm->tm_year + 1900) % 100, 2);
  if (spec == 'Y') return ag_rt_strftime_put_num(dst, maxsize, pos, tm->tm_year + 1900, 4);
  if (spec == 'F') {
    return ag_rt_strftime_put_format(dst, maxsize, pos, 'Y', tm) &&
           ag_rt_strftime_put_char(dst, maxsize, pos, '-') &&
           ag_rt_strftime_put_format(dst, maxsize, pos, 'm', tm) &&
           ag_rt_strftime_put_char(dst, maxsize, pos, '-') &&
           ag_rt_strftime_put_format(dst, maxsize, pos, 'd', tm);
  }
  if (spec == 'T' || spec == 'X') {
    return ag_rt_strftime_put_format(dst, maxsize, pos, 'H', tm) &&
           ag_rt_strftime_put_char(dst, maxsize, pos, ':') &&
           ag_rt_strftime_put_format(dst, maxsize, pos, 'M', tm) &&
           ag_rt_strftime_put_char(dst, maxsize, pos, ':') &&
           ag_rt_strftime_put_format(dst, maxsize, pos, 'S', tm);
  }
  if (spec == 'x') return ag_rt_strftime_put_format(dst, maxsize, pos, 'F', tm);
  if (spec == 'c') {
    return ag_rt_strftime_put_format(dst, maxsize, pos, 'a', tm) &&
           ag_rt_strftime_put_char(dst, maxsize, pos, ' ') &&
           ag_rt_strftime_put_format(dst, maxsize, pos, 'b', tm) &&
           ag_rt_strftime_put_char(dst, maxsize, pos, ' ') &&
           ag_rt_strftime_put_format(dst, maxsize, pos, 'd', tm) &&
           ag_rt_strftime_put_char(dst, maxsize, pos, ' ') &&
           ag_rt_strftime_put_format(dst, maxsize, pos, 'T', tm) &&
           ag_rt_strftime_put_char(dst, maxsize, pos, ' ') &&
           ag_rt_strftime_put_format(dst, maxsize, pos, 'Y', tm);
  }
  if (!ag_rt_strftime_put_char(dst, maxsize, pos, '%')) return 0;
  return ag_rt_strftime_put_char(dst, maxsize, pos, spec);
}

unsigned long __agc_runtime_strftime(long s_addr, unsigned long maxsize, long format_addr, long timeptr_addr) {
  char *dst = (char *)ag_rt_ptr(s_addr);
  char *fmt = (char *)ag_rt_ptr(format_addr);
  struct ag_rt_tm *tm = (struct ag_rt_tm *)ag_rt_ptr(timeptr_addr);
  long pos = 0;
  if (!dst || !fmt || !tm || maxsize == 0) return 0;
  while (*fmt) {
    if (*fmt == '%') {
      fmt++;
      if (!*fmt) {
        if (!ag_rt_strftime_put_char(dst, (long)maxsize, &pos, '%')) return 0;
        break;
      }
      if (!ag_rt_strftime_put_format(dst, (long)maxsize, &pos, *fmt, tm)) return 0;
    } else {
      if (!ag_rt_strftime_put_char(dst, (long)maxsize, &pos, *fmt)) return 0;
    }
    fmt++;
  }
  dst[pos] = 0;
  return (unsigned long)pos;
}

int __agc_runtime_getrusage(int who, long usage_addr) {
  (void)who;
  if (usage_addr) {
    long *usage = (long *)ag_rt_ptr(usage_addr);
    usage[0] = 0;
  }
  return 0;
}

static char ag_rt_getline_tmp[AG_RT_FILE_BUF_CAP];

static int ag_rt_getline_read_char(struct ag_rt_file *f) {
  char *src;
  long stream_len;
  int ch;
  if (!f) return -1;
  if (f->write_mode) {
    f->error = 1;
    return -1;
  }
  if (f->has_ungetc) {
    f->has_ungetc = 0;
    f->eof = 0;
    return f->ungetc_ch;
  }
  src = ag_rt_stream_buf(f);
  stream_len = ag_rt_stream_len(f);
  if (f->pos >= stream_len) {
    f->eof = 1;
    return -1;
  }
  ch = (int)(unsigned char)src[f->pos];
  ag_rt_file_set_pos(f, f->pos + 1);
  return ch;
}

long __agc_runtime_getline(long lineptr_addr, long n_addr, long stream_addr) {
  char **lineptr = (char **)ag_rt_ptr(lineptr_addr);
  unsigned long *cap = (unsigned long *)ag_rt_ptr(n_addr);
  struct ag_rt_file *f;
  long len;
  long need;
  long new_cap;
  char *dst;
  int ch;
  long i;
  if (!lineptr || !cap) return -1;
  f = ag_rt_input_stream(stream_addr);
  if (!f) return -1;
  if (f->write_mode) {
    f->error = 1;
    return -1;
  }
  len = 0;
  while (len + 1 < AG_RT_FILE_BUF_CAP) {
    ch = ag_rt_getline_read_char(f);
    if (ch < 0) break;
    ag_rt_getline_tmp[len++] = (char)ch;
    if (ch == '\n') break;
  }
  if (len == 0) return -1;
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
    dst[i] = ag_rt_getline_tmp[i];
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
