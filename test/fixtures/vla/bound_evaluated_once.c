#include <assert.h>
#include <stddef.h>

static int next_extent(int *calls) {
    *calls += 1;
    return *calls + 1;
}

int main(void) {
    int calls = 0;
    int values[next_extent(&calls)]
              [next_extent(&calls)]
              [next_extent(&calls)];
    assert(calls == 3);
    assert(sizeof(values) == (size_t)(2 * 3 * 4 * 4));
    values[1][2][3] = 42;
    assert(values[1][2][3] == 42);
    return 0;
}
