// Designated zero-initialization of an aggregate member containing function
// pointers. This keeps coverage close to the `{0}` aggregate no-op bug while
// exercising the nested member-initializer path.
#include <assert.h>

struct Ops {
    int (*i)(int);
    double (*d)(double);
};

struct Holder {
    int marker;
    struct Ops ops[2];
    struct Ops *p;
};

int add1(int x) { return x + 1; }
double dbl(double x) { return x * 2.0; }

struct Holder gh0 = {.ops = {0}, .p = 0};
struct Holder gh1 = {.marker = 7, .ops = {[1] = {.i = add1, .d = dbl}}};

int use_static(void) {
    static struct Holder sh = {.ops = {0}, .p = 0};
    assert(sh.marker == 0);
    assert(sh.ops[0].i == 0);
    assert(sh.ops[1].d == 0);
    assert(sh.p == 0);
    sh.ops[0].i = add1;
    sh.ops[0].d = dbl;
    sh.p = &sh.ops[0];
    return sh.p->i(41) + (int)sh.p->d(21.0);
}

int main(void) {
    struct Holder h = {.ops = {0}, .p = 0};
    assert(gh0.marker == 0);
    assert(gh0.ops[0].i == 0);
    assert(gh0.ops[1].d == 0);
    assert(gh0.p == 0);
    assert(gh1.marker == 7);
    assert(gh1.ops[0].i == 0);
    assert(gh1.ops[1].i(41) == 42);
    assert((int)gh1.ops[1].d(21.0) == 42);

    assert(h.marker == 0);
    assert(h.ops[0].i == 0);
    assert(h.ops[1].d == 0);
    assert(h.p == 0);
    h.ops[1].i = add1;
    h.ops[1].d = dbl;
    h.p = &h.ops[1];
    assert(h.p->i(41) == 42);
    assert((int)h.p->d(21.0) == 42);
    assert(use_static() == 84);
    return 0;
}
