/*
 * A non-void function that can reach its closing brace has undefined
 * behavior only when that path is executed. Translation must still succeed
 * for aggregate and complex return types, including a source path ending in
 * a _Noreturn call.
 * Expected: exit=0
 */
#include <complex.h>

typedef struct {
    long first;
    int second;
} pair_t;

static _Noreturn void stop(void) {
    for (;;) {
    }
}

static _Noreturn void stop_from_prototype(void);

static pair_t select_pair(int use_value) {
    if (use_value) {
        return (pair_t){40, 2};
    }
    stop();
}

static double _Complex select_complex(int use_value) {
    if (use_value) {
        return 40.0 + 2.0 * I;
    }
    stop();
}

static int select_from_prototype(int use_value) {
    if (use_value) {
        return 13;
    }
    stop_from_prototype();
}

static int select_from_block_declaration(int use_value) {
    _Noreturn void stop_from_block_declaration(void);
    if (use_value) {
        return 29;
    }
    stop_from_block_declaration();
}

static _Noreturn void stop_from_prototype(void) {
    for (;;) {
    }
}

void stop_from_block_declaration(void) {
    for (;;) {
    }
}

int main(void) {
    pair_t pair = select_pair(1);
    double _Complex complex_value = select_complex(1);

    if (pair.first + pair.second != 42) return 1;
    if (creal(complex_value) + cimag(complex_value) != 42.0) return 2;
    if (select_from_prototype(1) +
            select_from_block_declaration(1) != 42) return 3;
    return 0;
}
