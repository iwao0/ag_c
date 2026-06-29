// Nested designated zero-initialization for aggregate members that contain
// function pointers. This extends the struct-member-array `{0}` regression
// coverage through nested struct/union designator paths.
#include <assert.h>

struct Ops {
    int (*i)(int);
    double (*d)(double);
};

struct Holder {
    struct Ops ops[2];
    struct Ops *p;
};

union Slot {
    struct Holder h;
    int raw[6];
};

struct Wrap {
    union Slot slots[2];
    struct Holder direct[2];
};

int add1(int x) { return x + 1; }
double dbl(double x) { return x * 2.0; }

struct Wrap gw = {
    .slots = {[1] = {.h = {.ops = {0}, .p = 0}}},
    .direct = {[0] = {.ops = {0}, .p = 0}, [1] = {.ops = {[1] = {.i = add1, .d = dbl}}}},
};

int main(void) {
    struct Wrap w = {
        .slots = {[0] = {.h = {.ops = {0}, .p = 0}}},
        .direct = {[0] = {.ops = {0}, .p = 0}, [1] = {.ops = {[1] = {.i = add1, .d = dbl}}}},
    };
    assert(gw.slots[1].h.ops[0].i == 0);
    assert(gw.slots[1].h.ops[1].d == 0);
    assert(gw.slots[1].h.p == 0);
    assert(gw.direct[0].ops[0].i == 0);
    assert(gw.direct[0].p == 0);
    assert(gw.direct[1].ops[1].i(41) == 42);
    assert((int)gw.direct[1].ops[1].d(21.0) == 42);
    assert(w.slots[0].h.ops[0].i == 0);
    assert(w.slots[0].h.ops[1].d == 0);
    assert(w.slots[0].h.p == 0);
    assert(w.direct[0].ops[0].i == 0);
    assert(w.direct[0].p == 0);
    assert(w.direct[1].ops[1].i(41) == 42);
    assert((int)w.direct[1].ops[1].d(21.0) == 42);
    return 0;
}
