/* A variably-modified typedef evaluates its bound when control reaches the
 * declaration. Uses of that typedef must retain the resulting runtime bound
 * instead of re-reading the source expression or a later variable value. */
#include <assert.h>
#include <stddef.h>

static int next_bound(int *calls, int value) {
  *calls += 1;
  return value;
}

int main(void) {
  int calls = 0;
  int extent = 3;
  typedef int Row[next_bound(&calls, extent++)];

  assert(calls == 1);
  assert(extent == 4);

  Row first;
  extent = 9;
  Row second;
  assert(calls == 1);
  assert(sizeof(first) == (size_t)(3 * sizeof(int)));
  assert(sizeof(second) == sizeof(first));
  first[2] = 17;
  second[2] = 25;
  assert(first[2] + second[2] == 42);

  for (int pass = 0; pass < 2; ++pass) {
    int loop_extent = pass + 2;
    typedef int LoopRow[next_bound(&calls, loop_extent)];
    LoopRow values;
    assert(sizeof(values) == (size_t)(pass + 2) * sizeof(int));
    values[pass + 1] = 30 + pass;
    assert(values[pass + 1] == 30 + pass);
  }

  assert(calls == 3);
  return 0;
}
