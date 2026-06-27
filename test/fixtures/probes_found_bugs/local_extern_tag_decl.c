/*
 * Block-scope `extern struct/union T obj;` used to lose the extern storage
 * class in stmt.c's tag-keyword fast path. The declaration was parsed as a
 * fresh automatic object, so member access read an uninitialized stack slot.
 */
#include <assert.h>

struct S { int x; };
struct S gs = { 42 };

union U { int x; long y; };
union U gu = { 77 };

struct S obj = { 99 };
struct S *gsp = &obj;

int main(void) {
  extern struct S gs;
  extern union U gu;
  extern struct S *gsp;
  extern struct S late;

  assert(gs.x == 42);
  assert(gu.x == 77);
  assert(gsp->x == 99);
  assert(late.x == 123);
  return 0;
}

struct S late = { 123 };
