// Writes through aggregate array members initialize the direct local base for
// usage diagnostics; pointer subscripts remain ordinary pointer reads.
#include <assert.h>

struct Dispatch {
    int values[2][2];
    int (*ops[2])(int);
};

static int increment(int value) {
    return value + 1;
}

static int decrement(int value) {
    return value - 1;
}

int main(void) {
    struct Dispatch dispatch;

    dispatch.values[0][0] = 10;
    dispatch.values[0][1] = 20;
    dispatch.values[1][0] = 30;
    dispatch.values[1][1] = 40;
    dispatch.ops[0] = increment;
    dispatch.ops[1] = decrement;

    assert(dispatch.ops[0](dispatch.values[0][1]) == 21);
    assert(dispatch.ops[1](dispatch.values[1][1]) == 39);
    return 0;
}
