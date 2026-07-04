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

int __agc_runtime_isnan(double x);
int __agc_runtime_isinf(double x);
int __agc_runtime_isfinite(double x);
int __agc_runtime_signbit(double x);
double __agc_runtime_copysign(double x, double y);
int __agc_runtime_fegetround(void);
int __agc_runtime_feraiseexcept(int excepts);

double __agc_runtime_fmod(double x, double y) {
  if (__agc_runtime_isnan(x)) return x;
  if (__agc_runtime_isnan(y)) return y;
  if (y == 0.0 || __agc_runtime_isinf(x)) {
    double zero = 0.0;
    return zero / zero;
  }
  if (__agc_runtime_isinf(y) || x == 0.0) return x;
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

#define AG_RT_FE_UPWARD 0x00400000
#define AG_RT_FE_DOWNWARD 0x00800000
#define AG_RT_FE_TOWARDZERO 0x00C00000
#define AG_RT_FE_INEXACT 0x10

static double __agc_runtime_round_even(double x) {
  double t = __agc_runtime_trunc(x);
  double frac = __agc_runtime_fabs(x - t);
  if (frac > 0.5 || (frac == 0.5 && ((long)__agc_runtime_fabs(t) & 1))) {
    t = t + (__agc_runtime_signbit(x) ? -1.0 : 1.0);
  }
  return t;
}

double __agc_runtime_nearbyint(double x) {
  int mode;
  if (!__agc_runtime_isfinite(x) || x == 0.0) return x;
  mode = __agc_runtime_fegetround();
  if (mode == AG_RT_FE_UPWARD) return __agc_runtime_ceil(x);
  if (mode == AG_RT_FE_DOWNWARD) return __agc_runtime_floor(x);
  if (mode == AG_RT_FE_TOWARDZERO) return __agc_runtime_trunc(x);
  return __agc_runtime_round_even(x);
}

float __agc_runtime_nearbyintf(float x) {
  return (float)__agc_runtime_nearbyint((double)x);
}

long double __agc_runtime_nearbyintl(long double x) {
  return (long double)__agc_runtime_nearbyint((double)x);
}

double __agc_runtime_rint(double x) {
  double r = __agc_runtime_nearbyint(x);
  if (__agc_runtime_isfinite(x) && r != x) __agc_runtime_feraiseexcept(AG_RT_FE_INEXACT);
  return r;
}

float __agc_runtime_rintf(float x) {
  return (float)__agc_runtime_rint((double)x);
}

long double __agc_runtime_rintl(long double x) {
  return (long double)__agc_runtime_rint((double)x);
}

long __agc_runtime_lrint(double x) {
  return (long)__agc_runtime_rint(x);
}

long __agc_runtime_lrintf(float x) {
  return (long)__agc_runtime_rint((double)x);
}

long __agc_runtime_lrintl(long double x) {
  return (long)__agc_runtime_rint((double)x);
}

long long __agc_runtime_llrint(double x) {
  return (long long)__agc_runtime_rint(x);
}

long long __agc_runtime_llrintf(float x) {
  return (long long)__agc_runtime_rint((double)x);
}

long long __agc_runtime_llrintl(long double x) {
  return (long long)__agc_runtime_rint((double)x);
}

long __agc_runtime_lround(double x) {
  return (long)__agc_runtime_round(x);
}

long __agc_runtime_lroundf(float x) {
  return (long)__agc_runtime_round((double)x);
}

long __agc_runtime_lroundl(long double x) {
  return (long)__agc_runtime_round((double)x);
}

long long __agc_runtime_llround(double x) {
  return (long long)__agc_runtime_round(x);
}

long long __agc_runtime_llroundf(float x) {
  return (long long)__agc_runtime_round((double)x);
}

long long __agc_runtime_llroundl(long double x) {
  return (long long)__agc_runtime_round((double)x);
}

static long __agc_runtime_nearest_even_quot(double q) {
  int sign = 1;
  if (q < 0.0) {
    sign = -1;
    q = -q;
  }
  long n = (long)q;
  double frac = q - (double)n;
  if (frac > 0.5 || (frac == 0.5 && (n & 1))) n++;
  return sign < 0 ? -n : n;
}

static int __agc_runtime_remquo_quo_bits(double x, double y) {
  long q = __agc_runtime_nearest_even_quot(x / y);
  int sign = q < 0 ? -1 : 1;
  int bits;
  if (q < 0) q = -q;
  bits = (int)(q & 7);
  return sign < 0 ? -bits : bits;
}

double __agc_runtime_remainder(double x, double y) {
  if (__agc_runtime_isnan(x)) return x;
  if (__agc_runtime_isnan(y)) return y;
  if (y == 0.0 || __agc_runtime_isinf(x)) {
    double zero = 0.0;
    return zero / zero;
  }
  if (__agc_runtime_isinf(y)) return x;
  long q = __agc_runtime_nearest_even_quot(x / y);
  return x - (double)q * y;
}

float __agc_runtime_remainderf(float x, float y) {
  return (float)__agc_runtime_remainder((double)x, (double)y);
}

long double __agc_runtime_remainderl(long double x, long double y) {
  return (long double)__agc_runtime_remainder((double)x, (double)y);
}

double __agc_runtime_remquo(double x, double y, long quo_addr) {
  double result = __agc_runtime_remainder(x, y);
  if (quo_addr) {
    int *quo = (int *)ag_rt_ptr(quo_addr);
    *quo = (__agc_runtime_isfinite(x) && __agc_runtime_isfinite(y) && y != 0.0)
               ? __agc_runtime_remquo_quo_bits(x, y)
               : 0;
  }
  return result;
}

float __agc_runtime_remquof(float x, float y, long quo_addr) {
  return (float)__agc_runtime_remquo((double)x, (double)y, quo_addr);
}

long double __agc_runtime_remquol(long double x, long double y, long quo_addr) {
  return (long double)__agc_runtime_remquo((double)x, (double)y, quo_addr);
}

double __agc_runtime_fdim(double x, double y) {
  if (__agc_runtime_isnan(x)) return x;
  if (__agc_runtime_isnan(y)) return y;
  return x > y ? x - y : 0.0;
}

float __agc_runtime_fdimf(float x, float y) {
  return (float)__agc_runtime_fdim((double)x, (double)y);
}

long double __agc_runtime_fdiml(long double x, long double y) {
  return (long double)__agc_runtime_fdim((double)x, (double)y);
}

double __agc_runtime_fma(double x, double y, double z) {
  return x * y + z;
}

float __agc_runtime_fmaf(float x, float y, float z) {
  return (float)__agc_runtime_fma((double)x, (double)y, (double)z);
}

long double __agc_runtime_fmal(long double x, long double y, long double z) {
  return (long double)__agc_runtime_fma((double)x, (double)y, (double)z);
}

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

double __agc_runtime_scalbn(double x, int exp) {
  return __agc_runtime_ldexp(x, exp);
}

float __agc_runtime_scalbnf(float x, int exp) {
  return (float)__agc_runtime_scalbn((double)x, exp);
}

long double __agc_runtime_scalbnl(long double x, int exp) {
  return (long double)__agc_runtime_scalbn((double)x, exp);
}

double __agc_runtime_scalbln(double x, long exp) {
  return __agc_runtime_ldexp(x, (int)exp);
}

float __agc_runtime_scalblnf(float x, long exp) {
  return (float)__agc_runtime_scalbln((double)x, exp);
}

long double __agc_runtime_scalblnl(long double x, long exp) {
  return (long double)__agc_runtime_scalbln((double)x, exp);
}

int __agc_runtime_ilogb(double x) {
  double ax;
  int e = 0;
  if (__agc_runtime_isnan(x)) return -2147483647 - 1;
  if (__agc_runtime_isinf(x)) return 2147483647;
  if (x == 0.0) return -2147483647 - 1;
  ax = __agc_runtime_fabs(x);
  while (ax >= 2.0) {
    ax = ax / 2.0;
    e++;
  }
  while (ax < 1.0) {
    ax = ax * 2.0;
    e--;
  }
  return e;
}

int __agc_runtime_ilogbf(float x) {
  return __agc_runtime_ilogb((double)x);
}

int __agc_runtime_ilogbl(long double x) {
  return __agc_runtime_ilogb((double)x);
}

double __agc_runtime_logb(double x) {
  double zero = 0.0;
  if (__agc_runtime_isnan(x)) return x;
  if (__agc_runtime_isinf(x)) return x < 0.0 ? -x : x;
  if (x == 0.0) return -1.0 / zero;
  return (double)__agc_runtime_ilogb(x);
}

float __agc_runtime_logbf(float x) {
  return (float)__agc_runtime_logb((double)x);
}

long double __agc_runtime_logbl(long double x) {
  return (long double)__agc_runtime_logb((double)x);
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

double __agc_runtime_exp2(double x) {
  return __agc_runtime_exp(x * 0.6931471805599453);
}

double __agc_runtime_expm1(double x) {
  return __agc_runtime_exp(x) - 1.0;
}

double __agc_runtime_log1p(double x) {
  return __agc_runtime_log(1.0 + x);
}

double __agc_runtime_erf(double x) {
  double sign = 1.0;
  double t;
  double poly;
  if (__agc_runtime_isnan(x)) return x;
  if (__agc_runtime_isinf(x)) return __agc_runtime_signbit(x) ? -1.0 : 1.0;
  if (x < 0.0) {
    sign = -1.0;
    x = -x;
  }
  t = 1.0 / (1.0 + 0.3275911 * x);
  poly = (((((1.061405429 * t - 1.453152027) * t) + 1.421413741) * t -
           0.284496736) * t + 0.254829592) * t;
  return sign * (1.0 - poly * __agc_runtime_exp(-x * x));
}

float __agc_runtime_erff(float x) {
  return (float)__agc_runtime_erf((double)x);
}

long double __agc_runtime_erfl(long double x) {
  return (long double)__agc_runtime_erf((double)x);
}

double __agc_runtime_erfc(double x) {
  if (__agc_runtime_isnan(x)) return x;
  if (__agc_runtime_isinf(x)) return __agc_runtime_signbit(x) ? 2.0 : 0.0;
  return 1.0 - __agc_runtime_erf(x);
}

float __agc_runtime_erfcf(float x) {
  return (float)__agc_runtime_erfc((double)x);
}

long double __agc_runtime_erfcl(long double x) {
  return (long double)__agc_runtime_erfc((double)x);
}

float __agc_runtime_expf(float x) {
  return (float)__agc_runtime_exp((double)x);
}

long double __agc_runtime_expl(long double x) {
  return (long double)__agc_runtime_exp((double)x);
}

float __agc_runtime_exp2f(float x) {
  return (float)__agc_runtime_exp2((double)x);
}

long double __agc_runtime_exp2l(long double x) {
  return (long double)__agc_runtime_exp2((double)x);
}

float __agc_runtime_expm1f(float x) {
  return (float)__agc_runtime_expm1((double)x);
}

long double __agc_runtime_expm1l(long double x) {
  return (long double)__agc_runtime_expm1((double)x);
}

float __agc_runtime_logf(float x) {
  return (float)__agc_runtime_log((double)x);
}

long double __agc_runtime_logl(long double x) {
  return (long double)__agc_runtime_log((double)x);
}

float __agc_runtime_log1pf(float x) {
  return (float)__agc_runtime_log1p((double)x);
}

long double __agc_runtime_log1pl(long double x) {
  return (long double)__agc_runtime_log1p((double)x);
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
  double pi = 3.141592653589793;
  double half_pi = 1.5707963267948966;
  double quarter_pi = 0.7853981633974483;
  if (__agc_runtime_isnan(y)) return y;
  if (__agc_runtime_isnan(x)) return x;
  if (__agc_runtime_isinf(y)) {
    if (__agc_runtime_isinf(x)) {
      if (__agc_runtime_signbit(x)) {
        return __agc_runtime_signbit(y) ? -3.0 * quarter_pi : 3.0 * quarter_pi;
      }
      return __agc_runtime_signbit(y) ? -quarter_pi : quarter_pi;
    }
    return __agc_runtime_signbit(y) ? -half_pi : half_pi;
  }
  if (__agc_runtime_isinf(x)) {
    if (__agc_runtime_signbit(x)) return __agc_runtime_signbit(y) ? -pi : pi;
    return __agc_runtime_copysign(0.0, y);
  }
  if (y == 0.0 && x == 0.0) {
    if (__agc_runtime_signbit(x)) return __agc_runtime_signbit(y) ? -pi : pi;
    return y;
  }
  if (x > 0.0) return __agc_runtime_atan(y / x);
  if (x < 0.0) {
    if (!__agc_runtime_signbit(y)) return __agc_runtime_atan(y / x) + pi;
    return __agc_runtime_atan(y / x) - pi;
  }
  if (y > 0.0) return half_pi;
  if (y < 0.0) return -half_pi;
  return 0.0;
}

double __agc_runtime_asin(double x) {
  if (__agc_runtime_isnan(x)) return x;
  if (x < -1.0 || x > 1.0) {
    double zero = 0.0;
    return zero / zero;
  }
  return __agc_runtime_atan2(x, __agc_runtime_sqrt(1.0 - x * x));
}

double __agc_runtime_acos(double x) {
  if (__agc_runtime_isnan(x)) return x;
  if (x < -1.0 || x > 1.0) {
    double zero = 0.0;
    return zero / zero;
  }
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

float __agc_runtime_sinhf(float x) {
  return (float)__agc_runtime_sinh((double)x);
}

long double __agc_runtime_sinhl(long double x) {
  return (long double)__agc_runtime_sinh((double)x);
}

double __agc_runtime_cosh(double x) {
  double ex = __agc_runtime_exp(x);
  double em = __agc_runtime_exp(-x);
  return (ex + em) / 2.0;
}

float __agc_runtime_coshf(float x) {
  return (float)__agc_runtime_cosh((double)x);
}

long double __agc_runtime_coshl(long double x) {
  return (long double)__agc_runtime_cosh((double)x);
}

double __agc_runtime_tanh(double x) {
  double ex = __agc_runtime_exp(x);
  double em = __agc_runtime_exp(-x);
  return (ex - em) / (ex + em);
}

float __agc_runtime_tanhf(float x) {
  return (float)__agc_runtime_tanh((double)x);
}

long double __agc_runtime_tanhl(long double x) {
  return (long double)__agc_runtime_tanh((double)x);
}

double __agc_runtime_asinh(double x) {
  double ax;
  double r;
  if (__agc_runtime_isnan(x) || __agc_runtime_isinf(x)) return x;
  ax = __agc_runtime_fabs(x);
  r = __agc_runtime_log(ax + __agc_runtime_sqrt(ax * ax + 1.0));
  return __agc_runtime_signbit(x) ? -r : r;
}

float __agc_runtime_asinhf(float x) {
  return (float)__agc_runtime_asinh((double)x);
}

long double __agc_runtime_asinhl(long double x) {
  return (long double)__agc_runtime_asinh((double)x);
}

double __agc_runtime_acosh(double x) {
  double zero = 0.0;
  if (__agc_runtime_isnan(x)) return x;
  if (__agc_runtime_isinf(x)) return __agc_runtime_signbit(x) ? zero / zero : x;
  if (x < 1.0) return zero / zero;
  if (x == 1.0) return 0.0;
  return __agc_runtime_log(x + __agc_runtime_sqrt(x - 1.0) * __agc_runtime_sqrt(x + 1.0));
}

float __agc_runtime_acoshf(float x) {
  return (float)__agc_runtime_acosh((double)x);
}

long double __agc_runtime_acoshl(long double x) {
  return (long double)__agc_runtime_acosh((double)x);
}

double __agc_runtime_atanh(double x) {
  double zero = 0.0;
  double ax;
  if (__agc_runtime_isnan(x)) return x;
  ax = __agc_runtime_fabs(x);
  if (ax > 1.0 || __agc_runtime_isinf(x)) return zero / zero;
  if (x == 1.0) return 1.0 / zero;
  if (x == -1.0) return -1.0 / zero;
  return 0.5 * __agc_runtime_log((1.0 + x) / (1.0 - x));
}

float __agc_runtime_atanhf(float x) {
  return (float)__agc_runtime_atanh((double)x);
}

long double __agc_runtime_atanhl(long double x) {
  return (long double)__agc_runtime_atanh((double)x);
}

double __agc_runtime_hypot(double x, double y) {
  double ax = __agc_runtime_fabs(x);
  double ay = __agc_runtime_fabs(y);
  double r;
  if (__agc_runtime_isinf(ax) || __agc_runtime_isinf(ay)) return 1.0 / 0.0;
  if (__agc_runtime_isnan(ax)) return ax;
  if (__agc_runtime_isnan(ay)) return ay;
  if (ax < ay) {
    double t = ax;
    ax = ay;
    ay = t;
  }
  if (ay == 0.0) return ax;
  r = ay / ax;
  return ax * __agc_runtime_sqrt(1.0 + r * r);
}

float __agc_runtime_hypotf(float x, float y) {
  return (float)__agc_runtime_hypot((double)x, (double)y);
}

long double __agc_runtime_hypotl(long double x, long double y) {
  return (long double)__agc_runtime_hypot((double)x, (double)y);
}

double __agc_runtime_fmin(double x, double y) {
  if (__agc_runtime_isnan(x)) return y;
  if (__agc_runtime_isnan(y)) return x;
  if (x == 0.0 && y == 0.0) return __agc_runtime_signbit(x) ? x : y;
  return x < y ? x : y;
}

float __agc_runtime_fminf(float x, float y) {
  return (float)__agc_runtime_fmin((double)x, (double)y);
}

long double __agc_runtime_fminl(long double x, long double y) {
  return (long double)__agc_runtime_fmin((double)x, (double)y);
}

double __agc_runtime_fmax(double x, double y) {
  if (__agc_runtime_isnan(x)) return y;
  if (__agc_runtime_isnan(y)) return x;
  if (x == 0.0 && y == 0.0) return __agc_runtime_signbit(x) ? y : x;
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
