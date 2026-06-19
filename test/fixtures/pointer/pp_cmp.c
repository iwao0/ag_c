// 2 段ポインタの値比較 (*pp == &x)
// 期待: exit=1
#include <assert.h>
int main(void) {
    int x = 5;
    int *p = &x;
    int **pp = &p;
    assert(*pp == &x);
    return 0;
}
