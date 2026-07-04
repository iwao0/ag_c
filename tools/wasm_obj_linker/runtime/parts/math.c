double __agc_runtime_sqrt(double x) {
  if (x <= 0.0) return 0.0;
  double g = x > 1.0 ? x : 1.0;
  for (int i = 0; i < 12; i++) g = (g + x / g) / 2.0;
  return g;
}

float __agc_runtime_sqrtf(float x) {
  if (x <= 0.0f) return 0.0f;
  float g = x > 1.0f ? x : 1.0f;
  for (int i = 0; i < 8; i++) g = (g + x / g) / 2.0f;
  return g;
}

long double __agc_runtime_sqrtl(long double x) {
  return (long double)__agc_runtime_sqrt((double)x);
}

double __agc_runtime_pow(double x, double y) {
  long yi = (long)y;
  if ((double)yi == y) {
    double base = x;
    double result = 1.0;
    long n = yi < 0 ? -yi : yi;
    while (n > 0) {
      if (n & 1) result = result * base;
      base = base * base;
      n = n / 2;
    }
    return yi < 0 ? 1.0 / result : result;
  }
  if (x <= 0.0) return 0.0;
  return __agc_runtime_exp(y * __agc_runtime_log(x));
}

float __agc_runtime_powf(float x, float y) {
  return (float)__agc_runtime_pow((double)x, (double)y);
}

long double __agc_runtime_powl(long double x, long double y) {
  return (long double)__agc_runtime_pow((double)x, (double)y);
}

double __agc_runtime_fabs(double x) {
  return x < 0.0 ? -x : x;
}

float __agc_runtime_fabsf(float x) {
  return x < 0.0f ? -x : x;
}

long double __agc_runtime_fabsl(long double x) {
  return x < 0.0L ? -x : x;
}

double __agc_runtime_trunc(double x) {
  return (double)(long)x;
}

double __agc_runtime_floor(double x) {
  long i = (long)x;
  if ((double)i > x) i = i - 1;
  return (double)i;
}

double __agc_runtime_ceil(double x) {
  long i = (long)x;
  if ((double)i < x) i = i + 1;
  return (double)i;
}

double __agc_runtime_round(double x) {
  return x < 0.0 ? __agc_runtime_ceil(x - 0.5) : __agc_runtime_floor(x + 0.5);
}

float __agc_runtime_floorf(float x) {
  return (float)__agc_runtime_floor((double)x);
}

long double __agc_runtime_floorl(long double x) {
  return (long double)__agc_runtime_floor((double)x);
}

float __agc_runtime_ceilf(float x) {
  return (float)__agc_runtime_ceil((double)x);
}

long double __agc_runtime_ceill(long double x) {
  return (long double)__agc_runtime_ceil((double)x);
}

float __agc_runtime_roundf(float x) {
  return (float)__agc_runtime_round((double)x);
}

long double __agc_runtime_roundl(long double x) {
  return (long double)__agc_runtime_round((double)x);
}

float __agc_runtime_truncf(float x) {
  return (float)__agc_runtime_trunc((double)x);
}

long double __agc_runtime_truncl(long double x) {
  return (long double)__agc_runtime_trunc((double)x);
}

double __agc_runtime_sin(double x) {
  double pi = 3.141592653589793;
  double two_pi = 6.283185307179586;
  while (x > pi) x = x - two_pi;
  while (x < -pi) x = x + two_pi;
  double x2 = x * x;
  double term = x;
  double sum = x;
  term = -term * x2 / 6.0;
  sum = sum + term;
  term = -term * x2 / 20.0;
  sum = sum + term;
  term = -term * x2 / 42.0;
  sum = sum + term;
  term = -term * x2 / 72.0;
  sum = sum + term;
  term = -term * x2 / 110.0;
  sum = sum + term;
  return sum;
}

double __agc_runtime_cos(double x) {
  double pi = 3.141592653589793;
  double two_pi = 6.283185307179586;
  while (x > pi) x = x - two_pi;
  while (x < -pi) x = x + two_pi;
  double x2 = x * x;
  double term = 1.0;
  double sum = 1.0;
  term = -term * x2 / 2.0;
  sum = sum + term;
  term = -term * x2 / 12.0;
  sum = sum + term;
  term = -term * x2 / 30.0;
  sum = sum + term;
  term = -term * x2 / 56.0;
  sum = sum + term;
  term = -term * x2 / 90.0;
  sum = sum + term;
  return sum;
}

double __agc_runtime_tan(double x) {
  double c = __agc_runtime_cos(x);
  if (c == 0.0) return x < 0.0 ? -1.0e308 : 1.0e308;
  return __agc_runtime_sin(x) / c;
}

float __agc_runtime_sinf(float x) {
  return (float)__agc_runtime_sin((double)x);
}

long double __agc_runtime_sinl(long double x) {
  return (long double)__agc_runtime_sin((double)x);
}

float __agc_runtime_cosf(float x) {
  return (float)__agc_runtime_cos((double)x);
}

long double __agc_runtime_cosl(long double x) {
  return (long double)__agc_runtime_cos((double)x);
}

float __agc_runtime_tanf(float x) {
  return (float)__agc_runtime_tan((double)x);
}

long double __agc_runtime_tanl(long double x) {
  return (long double)__agc_runtime_tan((double)x);
}

double __agc_runtime_fmod(double x, double y) {
  if (y == 0.0) return 0.0;
  long q = (long)(x / y);
  double r = x - (double)q * y;
  if (x >= 0.0 && r < 0.0) r = r + __agc_runtime_fabs(y);
  if (x < 0.0 && r > 0.0) r = r - __agc_runtime_fabs(y);
  return r;
}

float __agc_runtime_fmodf(float x, float y) {
  return (float)__agc_runtime_fmod((double)x, (double)y);
}

long double __agc_runtime_fmodl(long double x, long double y) {
  return (long double)__agc_runtime_fmod((double)x, (double)y);
}

int __agc_runtime_isnan(double x);
int __agc_runtime_isinf(double x);
int __agc_runtime_isfinite(double x);
int __agc_runtime_signbit(double x);

double __agc_runtime_ldexp(double x, int exp) {
  if (!__agc_runtime_isfinite(x) || x == 0.0) return x;
  while (exp > 0) {
    x = x * 2.0;
    exp--;
  }
  while (exp < 0) {
    x = x / 2.0;
    exp++;
  }
  return x;
}

float __agc_runtime_ldexpf(float x, int exp) {
  return (float)__agc_runtime_ldexp((double)x, exp);
}

long double __agc_runtime_ldexpl(long double x, int exp) {
  return (long double)__agc_runtime_ldexp((double)x, exp);
}

double __agc_runtime_frexp(double x, long exp_addr) {
  int *out_exp = (int *)ag_rt_ptr(exp_addr);
  double ax;
  int e = 0;
  if (!out_exp) return x;
  if (x == 0.0 || !__agc_runtime_isfinite(x)) {
    *out_exp = 0;
    return x;
  }
  ax = __agc_runtime_fabs(x);
  while (ax >= 1.0) {
    ax = ax / 2.0;
    e++;
  }
  while (ax < 0.5) {
    ax = ax * 2.0;
    e--;
  }
  *out_exp = e;
  return __agc_runtime_signbit(x) ? -ax : ax;
}

float __agc_runtime_frexpf(float x, long exp_addr) {
  return (float)__agc_runtime_frexp((double)x, exp_addr);
}

long double __agc_runtime_frexpl(long double x, long exp_addr) {
  return (long double)__agc_runtime_frexp((double)x, exp_addr);
}

double __agc_runtime_modf(double x, long iptr_addr) {
  double *iptr = (double *)ag_rt_ptr(iptr_addr);
  double whole;
  if (!iptr) return x;
  if (__agc_runtime_isnan(x)) {
    *iptr = x;
    return x;
  }
  if (__agc_runtime_isinf(x)) {
    *iptr = x;
    return __agc_runtime_signbit(x) ? -0.0 : 0.0;
  }
  whole = __agc_runtime_trunc(x);
  *iptr = whole;
  return x - whole;
}

float __agc_runtime_modff(float x, long iptr_addr) {
  float *iptr = (float *)ag_rt_ptr(iptr_addr);
  double whole;
  if (!iptr) return x;
  if (__agc_runtime_isnan((double)x)) {
    *iptr = x;
    return x;
  }
  if (__agc_runtime_isinf((double)x)) {
    *iptr = x;
    return __agc_runtime_signbit((double)x) ? -0.0f : 0.0f;
  }
  whole = __agc_runtime_trunc((double)x);
  *iptr = (float)whole;
  return x - (float)whole;
}

long double __agc_runtime_modfl(long double x, long iptr_addr) {
  return (long double)__agc_runtime_modf((double)x, iptr_addr);
}

double __agc_runtime_copysign(double x, double y) {
  double ax = __agc_runtime_fabs(x);
  return __agc_runtime_signbit(y) ? -ax : ax;
}

float __agc_runtime_copysignf(float x, float y) {
  return (float)__agc_runtime_copysign((double)x, (double)y);
}

long double __agc_runtime_copysignl(long double x, long double y) {
  return (long double)__agc_runtime_copysign((double)x, (double)y);
}

double __agc_runtime_nan(long tagp_addr) {
  double z = 0.0;
  (void)tagp_addr;
  return z / z;
}

float __agc_runtime_nanf(long tagp_addr) {
  return (float)__agc_runtime_nan(tagp_addr);
}

long double __agc_runtime_nanl(long tagp_addr) {
  return (long double)__agc_runtime_nan(tagp_addr);
}

double __agc_runtime_cbrt(double x) {
  if (x == 0.0) return 0.0;
  double sign = x < 0.0 ? -1.0 : 1.0;
  double a = x < 0.0 ? -x : x;
  double g = a > 1.0 ? a : 1.0;
  for (int i = 0; i < 24; i++) g = (2.0 * g + a / (g * g)) / 3.0;
  return sign * g;
}

float __agc_runtime_cbrtf(float x) {
  return (float)__agc_runtime_cbrt((double)x);
}

long double __agc_runtime_cbrtl(long double x) {
  return (long double)__agc_runtime_cbrt((double)x);
}

double __agc_runtime_exp(double x) {
  double ln2 = 0.6931471805599453;
  int k = 0;
  while (x > ln2) {
    x = x - ln2;
    k++;
  }
  while (x < -ln2) {
    x = x + ln2;
    k--;
  }
  double term = 1.0;
  double sum = 1.0;
  for (int i = 1; i <= 18; i++) {
    term = term * x / (double)i;
    sum = sum + term;
  }
  while (k > 0) {
    sum = sum * 2.0;
    k--;
  }
  while (k < 0) {
    sum = sum / 2.0;
    k++;
  }
  return sum;
}

double __agc_runtime_log(double x) {
  if (x <= 0.0) return 0.0;
  int k = 0;
  while (x > 2.0) {
    x = x / 2.0;
    k++;
  }
  while (x < 0.5) {
    x = x * 2.0;
    k--;
  }
  double y = (x - 1.0) / (x + 1.0);
  double y2 = y * y;
  double term = y;
  double sum = 0.0;
  for (int n = 1; n <= 39; n = n + 2) {
    sum = sum + term / (double)n;
    term = term * y2;
  }
  return 2.0 * sum + (double)k * 0.6931471805599453;
}

double __agc_runtime_log2(double x) {
  return __agc_runtime_log(x) / 0.6931471805599453;
}

double __agc_runtime_log10(double x) {
  return __agc_runtime_log(x) / 2.302585092994046;
}

float __agc_runtime_expf(float x) {
  return (float)__agc_runtime_exp((double)x);
}

long double __agc_runtime_expl(long double x) {
  return (long double)__agc_runtime_exp((double)x);
}

float __agc_runtime_logf(float x) {
  return (float)__agc_runtime_log((double)x);
}

long double __agc_runtime_logl(long double x) {
  return (long double)__agc_runtime_log((double)x);
}

float __agc_runtime_log2f(float x) {
  return (float)__agc_runtime_log2((double)x);
}

long double __agc_runtime_log2l(long double x) {
  return (long double)__agc_runtime_log2((double)x);
}

float __agc_runtime_log10f(float x) {
  return (float)__agc_runtime_log10((double)x);
}

long double __agc_runtime_log10l(long double x) {
  return (long double)__agc_runtime_log10((double)x);
}

static double ag_rt_atan_core(double x) {
  double x2 = x * x;
  double term = x;
  double sum = x;
  double den = 3.0;
  int neg = 1;
  for (int i = 0; i < 96; i = i + 1) {
    term = term * x2;
    if (neg) sum = sum - term / den;
    else sum = sum + term / den;
    neg = !neg;
    den = den + 2.0;
  }
  return sum;
}

double __agc_runtime_atan(double x) {
  if (x > 1.0) return 1.5707963267948966 - ag_rt_atan_core(1.0 / x);
  if (x < -1.0) return -1.5707963267948966 - ag_rt_atan_core(1.0 / x);
  return ag_rt_atan_core(x);
}

double __agc_runtime_atan2(double y, double x) {
  if (x > 0.0) return __agc_runtime_atan(y / x);
  if (x < 0.0) {
    if (y >= 0.0) return __agc_runtime_atan(y / x) + 3.141592653589793;
    return __agc_runtime_atan(y / x) - 3.141592653589793;
  }
  if (y > 0.0) return 1.5707963267948966;
  if (y < 0.0) return -1.5707963267948966;
  return 0.0;
}

double __agc_runtime_asin(double x) {
  return __agc_runtime_atan2(x, __agc_runtime_sqrt(1.0 - x * x));
}

double __agc_runtime_acos(double x) {
  return __agc_runtime_atan2(__agc_runtime_sqrt(1.0 - x * x), x);
}

float __agc_runtime_atanf(float x) {
  return (float)__agc_runtime_atan((double)x);
}

long double __agc_runtime_atanl(long double x) {
  return (long double)__agc_runtime_atan((double)x);
}

float __agc_runtime_atan2f(float y, float x) {
  return (float)__agc_runtime_atan2((double)y, (double)x);
}

long double __agc_runtime_atan2l(long double y, long double x) {
  return (long double)__agc_runtime_atan2((double)y, (double)x);
}

float __agc_runtime_asinf(float x) {
  return (float)__agc_runtime_asin((double)x);
}

long double __agc_runtime_asinl(long double x) {
  return (long double)__agc_runtime_asin((double)x);
}

float __agc_runtime_acosf(float x) {
  return (float)__agc_runtime_acos((double)x);
}

long double __agc_runtime_acosl(long double x) {
  return (long double)__agc_runtime_acos((double)x);
}

double __agc_runtime_sinh(double x) {
  double ex = __agc_runtime_exp(x);
  double em = __agc_runtime_exp(-x);
  return (ex - em) / 2.0;
}

double __agc_runtime_cosh(double x) {
  double ex = __agc_runtime_exp(x);
  double em = __agc_runtime_exp(-x);
  return (ex + em) / 2.0;
}

double __agc_runtime_tanh(double x) {
  double ex = __agc_runtime_exp(x);
  double em = __agc_runtime_exp(-x);
  return (ex - em) / (ex + em);
}

double __agc_runtime_hypot(double x, double y) {
  return __agc_runtime_sqrt(x * x + y * y);
}

float __agc_runtime_hypotf(float x, float y) {
  return (float)__agc_runtime_hypot((double)x, (double)y);
}

long double __agc_runtime_hypotl(long double x, long double y) {
  return (long double)__agc_runtime_hypot((double)x, (double)y);
}

double __agc_runtime_fmin(double x, double y) {
  return x < y ? x : y;
}

float __agc_runtime_fminf(float x, float y) {
  return (float)__agc_runtime_fmin((double)x, (double)y);
}

long double __agc_runtime_fminl(long double x, long double y) {
  return (long double)__agc_runtime_fmin((double)x, (double)y);
}

double __agc_runtime_fmax(double x, double y) {
  return x > y ? x : y;
}

float __agc_runtime_fmaxf(float x, float y) {
  return (float)__agc_runtime_fmax((double)x, (double)y);
}

long double __agc_runtime_fmaxl(long double x, long double y) {
  return (long double)__agc_runtime_fmax((double)x, (double)y);
}

int __agc_runtime_isnan(double x) {
  return !(x <= 0.0 || x > 0.0);
}

int __agc_runtime_isinf(double x) {
  double max = 1.7976931348623157e308;
  return x > max || x < -max;
}

int __agc_runtime_isfinite(double x) {
  return !__agc_runtime_isnan(x) && !__agc_runtime_isinf(x);
}

int __agc_runtime_signbit(double x) {
  return x < 0.0 || (x == 0.0 && 1.0 / x < 0.0);
}

int __agc_runtime_fpclassify(double x) {
  double ax;
  double min_normal = 2.2250738585072014e-308;
  if (__agc_runtime_isnan(x)) return 0;
  if (__agc_runtime_isinf(x)) return 1;
  if (x == 0.0) return 2;
  ax = __agc_runtime_fabs(x);
  if (ax < min_normal) return 3;
  return 4;
}

int __agc_runtime_isnormal(double x) {
  return __agc_runtime_fpclassify(x) == 4;
}

int __agc_runtime_isunordered(double x, double y) {
  return __agc_runtime_isnan(x) || __agc_runtime_isnan(y);
}

int __agc_runtime_isgreater(double x, double y) {
  return !__agc_runtime_isunordered(x, y) && x > y;
}

int __agc_runtime_isgreaterequal(double x, double y) {
  return !__agc_runtime_isunordered(x, y) && x >= y;
}

int __agc_runtime_isless(double x, double y) {
  return !__agc_runtime_isunordered(x, y) && x < y;
}

int __agc_runtime_islessequal(double x, double y) {
  return !__agc_runtime_isunordered(x, y) && x <= y;
}

int __agc_runtime_islessgreater(double x, double y) {
  return !__agc_runtime_isunordered(x, y) && x != y;
}
