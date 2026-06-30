// Anonymous union members are promoted, but the union storage is shared.
// Global flat initialization must map `.a` and `.q` to the same union slot
// and then continue with the following struct member after the union storage.
#include <assert.h>

struct H {
  union {
    int a[2];
    int q;
  };
  int z;
};

struct H h = {.a = {1, 2}, .z = 3};
struct H k = {.q = 7, .z = 9};

int main(void) {
  struct H lh = {.a = {11, 12}, .z = 13};
  struct H lk = {.q = 17, .z = 19};

  assert(h.a[0] == 1 && h.a[1] == 2 && h.z == 3);
  assert(k.q == 7 && k.z == 9);
  assert(lh.a[0] == 11 && lh.a[1] == 12 && lh.z == 13);
  assert(lk.q == 17 && lk.z == 19);
  return 0;
}
