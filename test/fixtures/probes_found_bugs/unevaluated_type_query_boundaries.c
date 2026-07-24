/* sizeof and _Alignof suppress ordinary operand evaluation, while sizeof a
 * variably modified type evaluates the bound needed to compute its size. */
#include <assert.h>
#include <stddef.h>

struct over_aligned_query {
  _Alignas(32) char value;
};

enum {
  QUERY_AGGREGATE_ALIGNMENT = _Alignof(struct over_aligned_query),
  QUERY_UNEVALUATED_DIVISION = sizeof(1 / 0),
  QUERY_UNEVALUATED_DEREFERENCE = sizeof(*(int *)0)
};

_Static_assert(QUERY_AGGREGATE_ALIGNMENT == 32,
               "aggregate alignment is an integer constant");
_Static_assert(QUERY_UNEVALUATED_DIVISION == sizeof(int),
               "sizeof does not evaluate division");
_Static_assert(QUERY_UNEVALUATED_DEREFERENCE == sizeof(int),
               "sizeof does not dereference null");

int alignment_bound[
    _Alignof(struct over_aligned_query) == 32 ? 2 : -1];

struct query_widths {
  unsigned width : _Alignof(int);
};

_Alignas(_Alignof(struct over_aligned_query))
static char aligned_storage;

int main(void) {
  int side_effect = 0;
  int length = 3;
  struct query_widths widths = {15};

  assert(sizeof(side_effect++) == sizeof(int));
  assert(side_effect == 0);
  assert(sizeof(*(int *)0) == sizeof(int));
  assert(sizeof(1 / 0) == sizeof(int));
  assert(_Alignof(int[(side_effect++, length)]) ==
         _Alignof(int));
  assert(side_effect == 0);
  assert(sizeof(int[(side_effect++, length)]) ==
         3 * sizeof(int));
  assert(side_effect == 1);
  assert(widths.width == 15);
  assert(sizeof(alignment_bound) == 2 * sizeof(int));
  assert((size_t)&aligned_storage %
             _Alignof(struct over_aligned_query) ==
         0);
  return 0;
}
