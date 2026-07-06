#include <assert.h>

struct FVec {
  float f[3];
  double d[2];
};

int main(void) {
  struct FVec v = {
      .f[1] = 2.5f,
      .f[2] = 4.5f,
      .d[0] = 10.25,
      .d[1] = 31.75,
  };

  assert(v.f[0] == 0.0f);
  assert(v.f[1] == 2.5f);
  assert(v.f[2] == 4.5f);
  assert(v.d[0] + v.d[1] == 42.0);
  return 0;
}
