// 2 段ポインタ deref
// 期待: exit=42
#include <assert.h>
int main(void) {
    int x = 42;
    int *p = &x;
    int **pp = &p;
    assert(**pp == 42);
    return 0;
}
