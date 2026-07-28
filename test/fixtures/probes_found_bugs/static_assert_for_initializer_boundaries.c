/*
 * N1570 6.7 makes a static_assert-declaration a declaration, and 6.8.5
 * permits a declaration in the first clause of a for statement.  It declares
 * no identifier, so the auto/register object constraint is vacuously met.
 */
#include <assert.h>

#define REQUIRE_INT_WIDTH() \
  _Static_assert(sizeof(int) == 4, "int width")

static int count_iterations(void) {
  int count = 0;
  for (REQUIRE_INT_WIDTH(); count < 3; count++)
    ;
  return count;
}

static int nested_initializer(void) {
  int visits = 0;
  for (_Static_assert(1, "outer initializer"); visits < 2; visits++) {
    for (_Static_assert(_Alignof(long) > 0, "inner initializer"); ; )
      break;
  }
  return visits;
}

int main(void) {
  assert(count_iterations() == 3);
  assert(nested_initializer() == 2);
  return 0;
}
