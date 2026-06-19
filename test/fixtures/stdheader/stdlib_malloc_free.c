// stdlib.h の malloc/free
// 期待: exit=42
#include <stdlib.h>
#include <assert.h>
int main(void) {
    int *p = malloc(8);
    *p = 42;
    int v = *p;
    free(p);
    assert(v == 42);
    return 0;
}
