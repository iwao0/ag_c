// *(*pp + 2) で 3 番目要素を読む
// 期待: exit=30
#include <assert.h>
int main(void) {
    int a[4] = {10,20,30,40};
    int *p = a;
    int **pp = &p;
    assert(*(*pp + 2) == 30);
    return 0;
}
