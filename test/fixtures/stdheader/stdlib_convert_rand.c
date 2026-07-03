// stdlib.h conversion, labs, rand/srand
// Expected: exit=0
#include <assert.h>
#include <stdlib.h>

int main(void) {
    assert(atoi("42") == 42);
    assert(atoi("  -17x") == -17);
    assert(atoi("+8") == 8);
    assert(atol("123456") == 123456);
    assert(atol("\t-987") == -987);
    assert(labs(-123456789L) == 123456789L);
    assert(labs(55L) == 55L);

    srand(7);
    int a = rand();
    int b = rand();
    srand(7);
    assert(rand() == a);
    assert(rand() == b);
    return 0;
}
