// Definite-initialization state is merged across mutually exclusive branches.
// A local is initialized after a branch only when every reachable path writes
// it, while reads within each branch see that branch's own state.
#include <assert.h>

static int choose_with_if(int condition) {
    int value;
    if (condition) {
        value = 11;
    } else {
        value = 17;
    }
    return value;
}

static int choose_with_ternary(int condition) {
    int value;
    int selected = condition
        ? (value = 23)
        : (value = 29);
    assert(selected == value);
    return value;
}

static int short_circuit_after_initialization(int condition) {
    int value;
    (value = condition) && (value += 5);
    return value;
}

static int nested_all_paths_initialize(int outer, int inner) {
    int value;
    if (outer) {
        if (inner) {
            value = 31;
        } else {
            value = 37;
        }
    } else {
        value = 41;
    }
    return value;
}

int main(void) {
    assert(choose_with_if(0) == 17);
    assert(choose_with_if(1) == 11);
    assert(choose_with_ternary(0) == 29);
    assert(choose_with_ternary(1) == 23);
    assert(short_circuit_after_initialization(0) == 0);
    assert(short_circuit_after_initialization(1) == 6);
    assert(nested_all_paths_initialize(1, 1) == 31);
    assert(nested_all_paths_initialize(1, 0) == 37);
    assert(nested_all_paths_initialize(0, 0) == 41);
    return 0;
}
