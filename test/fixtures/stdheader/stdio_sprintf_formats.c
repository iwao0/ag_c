// stdio.h sprintf formats backed by the Wasm standalone runtime stub.
// Expected: exit=0
#include <stdio.h>
#include <string.h>
#include <assert.h>

int main(void) {
    char buf[32];
    int n = sprintf(buf, "%d-%u-%s-%c-%%", -12, 34u, "ok", 65);
    assert(n == 13);
    assert(strcmp(buf, "-12-34-ok-A-%") == 0);

    char padded[16];
    int m = sprintf(padded, "->%02d<-\n", 7);
    assert(m == 7);
    assert(strcmp(padded, "->07<-\n") == 0);
    return 0;
}
