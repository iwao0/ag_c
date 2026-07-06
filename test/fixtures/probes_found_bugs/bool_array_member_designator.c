#include <assert.h>

struct BVec {
  _Bool b[3];
};

int main(void) {
  struct BVec v = {
      .b[0] = 7,
      .b[1] = 0,
      .b[2] = -3,
  };

  assert(v.b[0] == 1);
  assert(v.b[1] == 0);
  assert(v.b[2] == 1);
  return 0;
}
