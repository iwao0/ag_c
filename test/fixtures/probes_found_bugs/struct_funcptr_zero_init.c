// `struct Holder h = {0};` where the first member is an aggregate containing
// function pointers used to reach E4007 in the IR path. The leading zero was
// treated as a 16-byte aggregate assignment to `ops` instead of relying on the
// pre-zero-fill of the whole object.
#include <assert.h>

struct Ops {
    double (*d)(double);
    float (*f)(float);
};

struct Holder {
    struct Ops ops[2];
    struct Ops *p;
};

struct Wrap {
    struct Holder h;
    int tail;
};

double dbl(double x) { return x * 2.0; }
float inc(float x) { return x + 1.0f; }

struct Holder gh = {0};
struct Wrap gw = {0};

int use_static(void) {
    static struct Holder sh = {0};
    assert(sh.ops[0].d == 0);
    assert(sh.ops[1].f == 0);
    assert(sh.p == 0);
    sh.ops[1].d = dbl;
    sh.ops[1].f = inc;
    sh.p = &sh.ops[1];
    return (int)sh.p->d(21.0) + (int)sh.p->f(41.0f);
}

int main(void) {
    struct Holder h = {0};
    struct Wrap w = {0};
    assert(gh.ops[0].d == 0);
    assert(gh.ops[1].f == 0);
    assert(gh.p == 0);
    assert(gw.h.ops[0].d == 0);
    assert(gw.tail == 0);
    assert(h.ops[0].d == 0);
    assert(h.ops[0].f == 0);
    assert(h.ops[1].d == 0);
    assert(h.ops[1].f == 0);
    assert(h.p == 0);
    assert(w.h.ops[0].d == 0);
    assert(w.h.ops[1].f == 0);
    assert(w.h.p == 0);

    h.ops[0].d = dbl;
    h.ops[0].f = inc;
    h.p = &h.ops[0];
    assert((int)h.ops[0].d(21.0) == 42);
    assert((int)h.p->f(41.0f) == 42);
    w.h.ops[0].d = dbl;
    w.h.ops[0].f = inc;
    w.h.p = &w.h.ops[0];
    assert((int)w.h.p->d(21.0) == 42);
    assert((int)w.h.p->f(41.0f) == 42);
    assert(use_static() == 84);
    return 0;
}
