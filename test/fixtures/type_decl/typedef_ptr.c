// typedef でポインタ型 alias
// 期待: exit=11
#include <assert.h>
typedef int *intptr;
int main(void) {
    int a = 11;
    intptr p = &a;
    assert(*p == 11);
    return 0;
}
