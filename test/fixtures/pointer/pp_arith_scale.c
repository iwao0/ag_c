// *pp = *pp + 2 でポインタ加算 (int ストライド) → 3 番目要素
// 期待: exit=30
#include <assert.h>
int main(void) {
    int a[4] = {10,20,30,40};
    int *p = a;
    int **pp = &p;
    *pp = *pp + 2;
    assert(*p == 30);
    return 0;
}
