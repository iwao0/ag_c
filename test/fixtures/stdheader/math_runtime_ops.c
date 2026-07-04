// math.h runtime helpers should link and return plausible values.
// expected: exit=0
#include <assert.h>
#include <fenv.h>
#include <math.h>

static int near1000(double v, int lo, int hi) {
  int n = (int)(v * 1000.0);
  return n >= lo && n <= hi;
}

static int near1000f(float v, int lo, int hi) {
  int n = (int)(v * 1000.0f);
  return n >= lo && n <= hi;
}

static int nanish(long double v) {
  return v != 0.0L && !(v < 0.0L) && !(v > 0.0L);
}

int main(void) {
  double z = 0.0;
  double nanv = z / z;
  double asin_domain = asin(2.0);
  double asin_nan = asin(nanv);
  double acos_domain = acos(2.0);
  double acos_nan = acos(nanv);
  float asinf_domain = asinf(2.0f);
  long double acosl_domain = acosl(2.0L);

  assert(near1000(sin(1.5707963267948966), 998, 1002));
  assert(near1000(cos(3.141592653589793), -1002, -998));
  assert(near1000(tan(0.7853981633974483), 998, 1002));
  assert(near1000(exp(1.0), 2716, 2720));
  assert(near1000(log(2.718281828459045), 998, 1002));
  assert(near1000(log2(8.0), 2998, 3002));
  assert(near1000(log10(100.0), 1998, 2002));
  assert(near1000(erf(1.0), 841, 844));
  assert(near1000f(erff(1.0f), 841, 844));
  assert(near1000(erfl(1.0L), 841, 844));
  assert(near1000(erfc(1.0), 156, 158));
  assert(near1000f(erfcf(1.0f), 156, 158));
  assert(near1000(erfcl(1.0L), 156, 158));
  assert(near1000(atan(1.0), 783, 787));
  assert(near1000(atan2(1.0, 0.0), 1568, 1572));
  assert(1.0 / atan2(-z, z) < 0.0);
  assert(near1000(atan2(z, -z), 3140, 3143));
  assert(near1000(atan2(-z, -z), -3143, -3140));
  assert(near1000(atan2(1.0 / z, 1.0 / z), 783, 787));
  assert(near1000(atan2(1.0 / z, -1.0 / z), 2354, 2358));
  assert(near1000(atan2(-1.0 / z, -1.0 / z), -2358, -2354));
  assert(near1000(asin(1.0), 1568, 1572));
  assert(nanish(asin_domain));
  assert(nanish(asin_nan));
  assert(nanish(asinf_domain));
  assert(near1000(acos(0.0), 1568, 1572));
  assert(nanish(acos_domain));
  assert(nanish(acos_nan));
  assert(nanish(acosl_domain));
  assert(near1000(sinh(0.0), 0, 0));
  assert(near1000(cosh(0.0), 998, 1002));
  assert(near1000(tanh(1.0), 759, 763));
  assert(near1000(asinh(1.0), 880, 882));
  assert(near1000f(asinhf(1.0f), 880, 882));
  assert(near1000(asinhl(1.0L), 880, 882));
  assert(near1000(acosh(2.0), 1315, 1317));
  assert(near1000f(acoshf(2.0f), 1315, 1317));
  assert(near1000(acoshl(2.0L), 1315, 1317));
  assert(near1000(atanh(0.5), 548, 550));
  assert(near1000f(atanhf(0.5f), 548, 550));
  assert(near1000(atanhl(0.5L), 548, 550));
  assert(near1000(fmod(7.5, 2.0), 1498, 1502));
  assert(near1000f(fmodf(7.5f, 2.0f), 1498, 1502));
  assert(near1000(fmodl(7.5L, 2.0L), 1498, 1502));
  assert(nanish(fmod(7.5, 0.0)));
  assert(nanish(fmod(1.0 / z, 2.0)));
  assert((int)fmod(7.5, 1.0 / z) == 7);
  assert(1.0 / fmod(-z, 3.0) < 0.0);
  assert(nanish(fmodf(7.5f, 0.0f)));
  assert(nanish(fmodl(7.5L, 0.0L)));
  assert(near1000(hypot(3.0, 4.0), 4998, 5002));
  assert(near1000f(hypotf(3.0f, 4.0f), 4998, 5002));
  assert(near1000(hypotl(3.0L, 4.0L), 4998, 5002));
  assert(hypot(1.0e200, 1.0e200) > 1.0e200);
  assert(hypot(1.0 / z, nanv) > 1.0e300);
  assert(nanish(hypot(nanv, 3.0)));
  assert(near1000(scalbn(0.75, 4), 11998, 12002));
  assert(near1000f(scalbnf(0.5f, 5), 15998, 16002));
  assert(near1000(scalbnl(0.25L, 6), 15998, 16002));
  assert(near1000(scalbln(1.5, 3L), 11998, 12002));
  assert(near1000f(scalblnf(1.25f, 2L), 4998, 5002));
  assert(near1000(scalblnl(3.0L, -1L), 1498, 1502));
  assert(ilogb(8.0) == 3);
  assert(ilogbf(0.75f) == -1);
  assert(ilogbl(0.25L) == -2);
  assert((int)logb(8.0) == 3);
  assert((int)logbf(0.75f) == -1);
  assert((int)logbl(0.25L) == -2);
  assert((int)fmin(nanv, 7.0) == 7);
  assert((int)fmin(7.0, nanv) == 7);
  assert((int)fminf((float)nanv, 5.0f) == 5);
  assert((int)fminl(6.0L, (long double)nanv) == 6);
  assert(1.0 / fmin(-z, z) < 0.0);
  assert(1.0 / fmin(z, -z) < 0.0);
  assert((int)fmax(nanv, 7.0) == 7);
  assert((int)fmax(7.0, nanv) == 7);
  assert((int)fmaxf((float)nanv, 5.0f) == 5);
  assert((int)fmaxl(6.0L, (long double)nanv) == 6);
  assert(1.0 / fmax(-z, z) > 0.0);
  assert(1.0 / fmax(z, -z) > 0.0);
  assert(near1000(cbrt(27.0), 2998, 3002));
  assert(near1000(pow(-2.0, 3.0), -8000, -8000));
  assert(near1000(pow(9.0, 0.5), 2998, 3002));
  assert(near1000f(powf(2.0f, 5.0f), 31998, 32002));
  assert(near1000(powl(2.0L, 4.0L), 15998, 16002));
  assert((int)floor(-3.2) == -4);
  assert((int)ceil(-3.8) == -3);
  assert((int)round(-3.5) == -4);
  assert((int)trunc(-3.8) == -3);
  assert(near1000(fabs(-2.5), 2498, 2502));
  assert(near1000f(fabsf(-2.5f), 2498, 2502));
  assert(near1000(fabsl(-4.5L), 4498, 4502));
  assert(near1000(sqrtl(2.0L), 1412, 1416));
  assert((int)floorf(2.9f) == 2);
  assert((int)ceilf(2.1f) == 3);
  assert((int)roundf(-2.5f) == -3);
  assert(nearbyint(2.5) == 2.0);
  assert(nearbyint(3.5) == 4.0);
  assert(nearbyintf(-2.5f) == -2.0f);
  assert(nearbyintl(-3.5L) == -4.0L);
  assert(lround(2.5) == 3);
  assert(lroundf(-2.5f) == -3);
  assert(lroundl(3.5L) == 4);
  assert(llround(-3.5) == -4);
  assert(llroundf(2.5f) == 3);
  assert(llroundl(-2.5L) == -3);
  assert(fesetround(FE_UPWARD) == 0);
  assert(rint(2.1) == 3.0);
  assert(rintf(-2.1f) == -2.0f);
  assert(lrint(2.1) == 3);
  assert(llrintf(-2.1f) == -2);
  assert(fesetround(FE_DOWNWARD) == 0);
  assert(rintl(2.9L) == 2.0L);
  assert(nearbyint(-2.1) == -3.0);
  assert(lrintl(2.9L) == 2);
  assert(llrintl(-2.1L) == -3);
  assert(fesetround(FE_TOWARDZERO) == 0);
  assert(rint(2.9) == 2.0);
  assert(rint(-2.9) == -2.0);
  assert(lrintf(2.9f) == 2);
  assert(llrint(-2.9) == -2);
  assert(fesetround(FE_TONEAREST) == 0);
  assert(rint(2.5) == 2.0);
  assert(rint(3.5) == 4.0);
  return 0;
}
