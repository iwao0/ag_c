// stdlib.h realloc minimal behavior
// Expected: exit=0
#include <assert.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    char *p = malloc(4);
    assert(p != 0);
    strcpy(p, "abc");

    char *q = realloc(p, 8);
    assert(q != 0);
    assert(strcmp(q, "abc") == 0);
    q[3] = 'd';
    q[4] = 0;
    assert(strcmp(q, "abcd") == 0);

    char *r = realloc(q, 2);
    assert(r != 0);
    assert(r[0] == 'a');
    assert(r[1] == 'b');

    char *s = realloc(0, 3);
    assert(s != 0);
    s[0] = 'x';
    s[1] = 0;
    assert(strcmp(s, "x") == 0);
    char *z = realloc(s, 0);
    if (z) free(z);
    return 0;
}
