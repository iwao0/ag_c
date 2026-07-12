/* Type-name syntax is bound after parsing. The saved lexical lookup point must
 * still select the typedef/tag that was visible where each expression appeared. */
#include <assert.h>

int main(void) {
  typedef int T;
  struct S { int outer; };
  typedef struct S Record;

  assert(sizeof(T) == 4);
  assert(_Generic((T)0, int: 1, default: 0));
  assert(sizeof(Record) == 4);

  {
    typedef long T;
    struct S { long inner; long tail; };
    typedef struct S Record;

    assert(sizeof(T) == 8);
    assert(_Generic((T)0, long: 1, default: 0));
    assert(sizeof(Record) == 16);
    Record value = {3, 4};
    assert(value.inner + value.tail == 7);
  }

  assert(sizeof(T) == 4);
  assert(sizeof(Record) == 4);
  Record value = {9};
  assert(value.outer == 9);
  return 0;
}
