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
struct N na[2] = {
  {{{71, 72}}, 73},
  {.a = {74, 75}, .z = 76},
};

int sum_n(struct N *p) {
  return p[0].a[0] + p[0].a[1] + p[0].z +
         p[1].a[0] + p[1].a[1] + p[1].z;
}

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
  struct N la[2] = {
    {{{81, 82}}, 83},
    {.a = {84, 85}, .z = 86},
  };
  struct N cn = (struct N){{{91, 92}}, 93};
  struct N dn = (struct N){.a = {94, 95}, .z = 96};

  assert(h.a[0] == 1 && h.a[1] == 2 && h.z == 3);
  assert(k.q == 7 && k.z == 9);
  assert(hp.a[0] == 21 && hp.a[1] == 22 && hp.z == 23);
  assert(n.a[0] == 31 && n.a[1] == 32 && n.z == 33);
  assert(sum_n(na) == 441);
  assert(lh.a[0] == 11 && lh.a[1] == 12 && lh.z == 13);
  assert(lk.q == 17 && lk.z == 19);
  assert(lp.a[0] == 51 && lp.a[1] == 52 && lp.z == 53);
  assert(ln.a[0] == 61 && ln.a[1] == 62 && ln.z == 63);
  assert(sum_n(la) == 501);
  assert(cn.a[0] == 91 && cn.a[1] == 92 && cn.z == 93);
  assert(dn.a[0] == 94 && dn.a[1] == 95 && dn.z == 96);
  assert(static_local() == 222);
  return 0;
}
