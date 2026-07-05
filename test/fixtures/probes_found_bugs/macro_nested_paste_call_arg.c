// `CAT(A,B)(x)` immediately feeds a stdio call argument while `xy` is local.
// c-testsuite 00201 exposed a streaming pushback bug where suffix rescan
// advanced the outer stream before the `xy` expansion was pushed back.
#include <assert.h>
#include <stdio.h>

#define CAT2(a, b) a##b
#define CAT(a, b) CAT2(a, b)
#define AB(x) CAT(x, y)

int main(void) {
    int xy = 42;
    printf("%d\n", CAT(A,B)(x));
    assert(CAT(A,B)(x) == 42);
    return 0;
}
