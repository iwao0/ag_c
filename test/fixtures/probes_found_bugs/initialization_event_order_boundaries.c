// Initializer and assignment writes become visible only after their source
// expressions have been evaluated. These initialized cases must remain free
// of false-positive local-usage warnings.
#include <assert.h>

struct Pair {
    int first;
    int second;
};

static void set_output(int *output, int value) {
    *output = value;
}

static int initialize_before_do_condition(void) {
    int value;
    do {
        value = 7;
    } while (value < 0);
    return value;
}

static int initialize_before_for_increment(void) {
    int value;
    for (int index = 0; index < 1; index++, value++) {
        value = 10;
    }
    return value;
}

int main(void) {
    int seed = 3, copy = seed + 2;
    int assigned;
    assigned = copy + 4;

    struct Pair pair = {
        assigned,
        seed
    };
    int values[2] = {
        pair.first,
        pair.second
    };

    int output;
    set_output(&output, values[0] + values[1]);

    int final_value;
    final_value = output;

    assert(seed == 3);
    assert(copy == 5);
    assert(assigned == 9);
    assert(pair.first == 9);
    assert(pair.second == 3);
    assert(values[0] == 9);
    assert(values[1] == 3);
    assert(output == 12);
    assert(final_value == 12);
    assert(initialize_before_do_condition() == 7);
    assert(initialize_before_for_increment() == 11);
    return 0;
}
