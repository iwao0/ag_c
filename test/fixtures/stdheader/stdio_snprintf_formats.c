// stdio.h snprintf formats backed by the Wasm standalone runtime stub.
// Expected: exit=0
#include <stdio.h>
#include <string.h>
#include <assert.h>

int main(void) {
    char neg[16];
    int n = snprintf(neg, sizeof(neg), "%d", -42);
    assert(n == 3);
    assert(strcmp(neg, "-42") == 0);

    char padded[16];
    int p = snprintf(padded, sizeof(padded), "%02d", 7);
    assert(p == 2);
    assert(strcmp(padded, "07") == 0);

    char pair[16];
    int q = snprintf(pair, sizeof(pair), "%d-%d", -12, 34);
    assert(q == 6);
    assert(strcmp(pair, "-12-34") == 0);

    char small[4];
    int t = snprintf(small, sizeof(small), "%d", 12345);
    assert(t == 5);
    assert(strcmp(small, "123") == 0);
    return 0;
}
