// (*pp)[3] で 4 番目要素を読む
// 期待: exit=40
#include <assert.h>
int main(void) {
    int a[4] = {10,20,30,40};
    int *p = a;
    int **pp = &p;
    assert((*pp)[3] == 40);
    return 0;
}
