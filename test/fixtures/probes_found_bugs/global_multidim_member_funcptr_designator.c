/* Global/static aggregate designator for multidimensional struct/union member arrays.
 * Before the fix, `.ops[1][0].f[1] = add2` advanced only one flat slot for the
 * first subscript, so the function pointer was emitted into the wrong element. */
#include <assert.h>

int add2(int x) { return x + 2; }
int add3(int x) { return x + 3; }

struct Ops { int (*f[2])(int); };
union UOps { int (*f[2])(int); long raw; };

struct SWrap { struct Ops ops[2][2]; };
struct UWrap { union UOps ops[2][2]; };

struct SWrap gs = {.ops[1][0].f[1] = add2};
struct UWrap gu = {.ops[1][0].f[1] = add3};

int main(void) {
  static struct SWrap ss = {.ops[1][0].f[1] = add2};
  static struct UWrap su = {.ops[1][0].f[1] = add3};

  assert(gs.ops[1][0].f[1](40) == 42);
  assert(gu.ops[1][0].f[1](40) == 43);
  assert(ss.ops[1][0].f[1](40) == 42);
  assert(su.ops[1][0].f[1](40) == 43);
  return 0;
}
