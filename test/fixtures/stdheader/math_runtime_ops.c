// math.h runtime helpers should link and return plausible values.
// expected: exit=0
#include <assert.h>
#include <math.h>

static int near1000(double v, int lo, int hi) {
  int n = (int)(v * 1000.0);
  return n >= lo && n <= hi;
}

static int near1000f(float v, int lo, int hi) {
  int n = (int)(v * 1000.0f);
  return n >= lo && n <= hi;
}

int main(void) {
  assert(near1000(sin(1.5707963267948966), 998, 1002));
  assert(near1000(cos(3.141592653589793), -1002, -998));
  assert(near1000(tan(0.7853981633974483), 998, 1002));
  assert(near1000(exp(1.0), 2716, 2720));
  assert(near1000(log(2.718281828459045), 998, 1002));
  assert(near1000(log2(8.0), 2998, 3002));
  assert(near1000(log10(100.0), 1998, 2002));
  assert(near1000(atan(1.0), 783, 787));
  assert(near1000(atan2(1.0, 0.0), 1568, 1572));
  assert(near1000(asin(1.0), 1568, 1572));
  assert(near1000(acos(0.0), 1568, 1572));
  assert(near1000(sinh(0.0), 0, 0));
  assert(near1000(cosh(0.0), 998, 1002));
  assert(near1000(tanh(1.0), 759, 763));
  assert(near1000(fmod(7.5, 2.0), 1498, 1502));
  assert(near1000f(fmodf(7.5f, 2.0f), 1498, 1502));
  assert(near1000(fmodl(7.5L, 2.0L), 1498, 1502));
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
  return 0;
}
