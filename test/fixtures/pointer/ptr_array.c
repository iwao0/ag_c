// ポインタ配列 (int* ptrs[3])
// 期待: exit=6 (1+2+3)
#include <assert.h>
int main(void) {
    int a = 1;
    int b = 2;
    int c = 3;
    int *ptrs[3];
    ptrs[0] = &a;
    ptrs[1] = &b;
    ptrs[2] = &c;
    assert(*ptrs[0] == 1);
    assert(*ptrs[1] == 2);
    assert(*ptrs[2] == 3);
    return 0;
}
