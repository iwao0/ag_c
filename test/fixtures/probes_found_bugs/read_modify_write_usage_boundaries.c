// Read-modify-write expressions require a prior value, while a preceding
// initializer or simple assignment makes that value available.
#include <assert.h>

struct Accumulator {
    int total;
    unsigned char wrapped;
    int values[2];
};

static int increment_parameter(int value) {
    return ++value;
}

static int increment_static(void) {
    static int value;
    value += 2;
    return value;
}

int main(void) {
    int scalar;
    struct Accumulator accumulator;

    scalar = 5;
    scalar += 7;
    --scalar;

    accumulator.total = 3;
    accumulator.total *= 4;
    accumulator.wrapped = 250;
    accumulator.wrapped += 10;
    accumulator.values[0] = 7;
    accumulator.values[1] = 11;
    accumulator.values[0]++;
    --accumulator.values[1];

    int *element = &accumulator.values[1];
    *element += 5;

    assert(scalar == 11);
    assert(accumulator.total == 12);
    assert(accumulator.wrapped == 4);
    assert(accumulator.values[0] == 8);
    assert(accumulator.values[1] == 15);
    assert(increment_parameter(4) == 5);
    assert(increment_static() == 2);
    assert(increment_static() == 4);
    return 0;
}
