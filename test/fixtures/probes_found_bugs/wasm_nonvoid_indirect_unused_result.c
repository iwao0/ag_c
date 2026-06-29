// Wasm used to reject unknown indirect non-void calls when the result was unused.
// The result type is still known from the function pointer type, so the call can
// be emitted as drop(call_indirect ... (result T)).
#include <assert.h>

int set7(int *p) {
    *p = 7;
    return 70;
}

int set9(int *p) {
    *p = 9;
    return 90;
}

int (*g)(int *) = set9;

struct Ops {
    int (*f)(int *);
};

struct Ops ops = {set9};

int call_global(int pick) {
    int x = 0;
    if (pick) {
        g = set7;
    }
    g(&x);
    return x;
}

int call_member(int pick) {
    int x = 0;
    if (pick) {
        ops.f = set7;
    }
    ops.f(&x);
    return x;
}

int main(void) {
    assert(call_global(1) == 7);
    g = set9;
    assert(call_global(0) == 9);
    assert(call_member(1) == 7);
    ops.f = set9;
    assert(call_member(0) == 9);
    return 0;
}
