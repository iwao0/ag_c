/* Keep all four C identifier namespaces distinct while semantic binding runs
 * after parsing has left the nested scopes. */
#include <assert.h>

struct Word {
  int Word;
};

static int resolve_after_parse(void) {
  typedef int Word;
  Word outer = 3;
  struct Word record = {4};

  {
    enum { outer = 5 };
    struct Word {
      long Word;
      long tail;
    };
    struct Word inner = {6, 7};

    assert(outer == 5);
    assert(inner.Word == 6);
    assert(inner.tail == 7);
  }

  assert(outer == 3);
  assert(record.Word == 4);
  goto Word;

Word:
  return outer + record.Word;
}

int main(void) {
  assert(resolve_after_parse() == 7);
  return 0;
}
