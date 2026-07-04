// tgmath.h dispatch should link float and long double math variants.
// expected: exit=0
#include <assert.h>
#include <tgmath.h>

static int near1000(double v, int lo, int hi) {
  int n = (int)(v * 1000.0);
  return n >= lo && n <= hi;
}

int main(void) {
  float f = 2.0f;
  long double l = 2.0L;

  assert(near1000(sqrt(f), 1412, 1416));
  assert(near1000(sqrt(l), 1412, 1416));
  assert(near1000(cbrt(27.0f), 2998, 3002));
  assert(near1000(cbrt(-8.0L), -2002, -1998));

  assert(near1000(sin(1.5707963267948966f), 998, 1002));
  assert(near1000(sin(1.5707963267948966L), 998, 1002));
  assert(near1000(cos(3.141592653589793f), -1002, -998));
  assert(near1000(cos(3.141592653589793L), -1002, -998));
  assert(near1000(tan(0.7853981633974483f), 998, 1002));
  assert(near1000(tan(0.7853981633974483L), 998, 1002));
  assert(near1000(asinh(1.0f), 880, 882));
  assert(near1000(asinh(1.0L), 880, 882));
  assert(near1000(acosh(2.0f), 1315, 1317));
  assert(near1000(acosh(2.0L), 1315, 1317));
  assert(near1000(atanh(0.5f), 548, 550));
  assert(near1000(atanh(0.5L), 548, 550));

  assert(near1000(asin(1.0f), 1568, 1572));
  assert(near1000(asin(1.0L), 1568, 1572));
  assert(near1000(acos(0.0f), 1568, 1572));
  assert(near1000(acos(0.0L), 1568, 1572));
  assert(near1000(atan(1.0f), 783, 787));
  assert(near1000(atan(1.0L), 783, 787));
  assert(near1000(atan2(1.0f, 0.0f), 1568, 1572));
  assert(near1000(atan2(1.0L, 0.0L), 1568, 1572));

  assert(near1000(exp(1.0f), 2716, 2720));
  assert(near1000(exp(1.0L), 2716, 2720));
  assert(near1000(log(2.718281828459045f), 998, 1002));
  assert(near1000(log(2.718281828459045L), 998, 1002));
  assert(near1000(log2(8.0f), 2998, 3002));
  assert(near1000(log2(8.0L), 2998, 3002));
  assert(near1000(log10(100.0f), 1998, 2002));
  assert(near1000(log10(100.0L), 1998, 2002));
  assert(near1000(erf(1.0f), 841, 844));
  assert(near1000(erf(1.0L), 841, 844));
  assert(near1000(erfc(1.0f), 156, 158));
  assert(near1000(erfc(1.0L), 156, 158));

  assert((int)floor(2.9L) == 2);
  assert((int)ceil(2.1L) == 3);
  assert((int)round(-2.5L) == -3);
  assert((int)trunc(-2.8f) == -2);
  assert((int)trunc(-2.8L) == -2);
  assert(nearbyint(2.5f) == 2.0f);
  assert(nearbyint(3.5L) == 4.0L);
  assert(rint(2.5f) == 2.0f);
  assert(rint(-3.5L) == -4.0L);
  assert(lrint(3.5f) == 4);
  assert(llrint(-2.5L) == -2);
  assert(lround(2.5f) == 3);
  assert(llround(-2.5L) == -3);
  assert(near1000(fabs(-2.5f), 2498, 2502));
  assert(near1000(fabs(-4.5L), 4498, 4502));

  assert(near1000(pow(2.0f, 5.0f), 31998, 32002));
  assert(near1000(pow(2.0L, 4.0L), 15998, 16002));
  assert(near1000(fmod(7.5f, 2.0f), 1498, 1502));
  assert(near1000(fmod(7.5L, 2.0L), 1498, 1502));
  assert(near1000(scalbn(0.75f, 4), 11998, 12002));
  assert(near1000(scalbn(0.25L, 6), 15998, 16002));
  assert(near1000(scalbln(1.25f, 2L), 4998, 5002));
  assert(near1000(scalbln(3.0L, -1L), 1498, 1502));
  assert(ilogb(0.75f) == -1);
  assert(ilogb(8.0L) == 3);
  assert((int)logb(0.75f) == -1);
  assert((int)logb(8.0L) == 3);
  assert(near1000(hypot(3.0f, 4.0f), 4998, 5002));
  assert(near1000(hypot(3.0L, 4.0L), 4998, 5002));
  assert(near1000(fmin(3.0f, 4.0f), 2998, 3002));
  assert(near1000(fmin(3.0L, 4.0L), 2998, 3002));
  assert(near1000(fmax(3.0f, 4.0f), 3998, 4002));
  assert(near1000(fmax(3.0L, 4.0L), 3998, 4002));

  return 0;
}
