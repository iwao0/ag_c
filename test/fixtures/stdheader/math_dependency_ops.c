// math.h helpers that depend on other emitted stubs should work by themselves.
// expected: exit=0
#include <assert.h>
#include <math.h>

static int near1000(double v, int lo, int hi) {
  int n = (int)(v * 1000.0);
  return n >= lo && n <= hi;
}

int main(void) {
  assert(near1000(tan(0.7853981633974483), 998, 1002));
  assert(near1000(log2(8.0), 2998, 3002));
  assert(near1000(asin(1.0), 1568, 1572));
  assert(near1000(tanh(1.0), 759, 763));
  return 0;
}
