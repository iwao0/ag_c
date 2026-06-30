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

struct N {
  struct {
    union {
      int a[2];
      int q;
    };
  };
  int z;
};

struct H h = {.a = {1, 2}, .z = 3};
struct H k = {.q = 7, .z = 9};
struct H hp = {{21, 22}, 23};
struct N n = {{{31, 32}}, 33};

int static_local(void) {
  static struct H sh = {.a = {41, 42}, .z = 43};
  static struct H sq = {.q = 47, .z = 49};
  return sh.a[0] + sh.a[1] + sh.z + sq.q + sq.z;
}

int main(void) {
  struct H lh = {.a = {11, 12}, .z = 13};
  struct H lk = {.q = 17, .z = 19};
  struct H lp = {{51, 52}, 53};
  struct N ln = {{{61, 62}}, 63};

  assert(h.a[0] == 1 && h.a[1] == 2 && h.z == 3);
  assert(k.q == 7 && k.z == 9);
  assert(hp.a[0] == 21 && hp.a[1] == 22 && hp.z == 23);
  assert(n.a[0] == 31 && n.a[1] == 32 && n.z == 33);
  assert(lh.a[0] == 11 && lh.a[1] == 12 && lh.z == 13);
  assert(lk.q == 17 && lk.z == 19);
  assert(lp.a[0] == 51 && lp.a[1] == 52 && lp.z == 53);
  assert(ln.a[0] == 61 && ln.a[1] == 62 && ln.z == 63);
  assert(static_local() == 222);
  return 0;
}
