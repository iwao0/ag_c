/* Keep only the active macro definition after #undef, conditional selection,
 * and a function-like to object-like transition. */
#include <assert.h>

#define VALUE 1
#undef VALUE
#define VALUE 4

#if 0
#undef VALUE
#define VALUE 99
#define HIDDEN 12
#else
#define BRANCH 5
#endif

#define APPLY(x) ((x) + VALUE)

#define SELECT(x) ((x) * 2)
#undef SELECT
#define SELECT 7

#define STABLE 3
#define STABLE 3

int main(void) {
  assert(VALUE == 4);
  assert(APPLY(6) == 10);
  assert(BRANCH == 5);
  assert(SELECT == 7);
  assert(STABLE == 3);
#ifdef HIDDEN
  assert(0);
#endif
  return 0;
}
