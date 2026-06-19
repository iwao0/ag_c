// struct メンバが関数ポインタ配列 [2] のとき sizeof = 16
// 期待: exit=16
#include <assert.h>
int main(void) {
    struct S { int (*arr[2])(int); };
    assert(sizeof(struct S) == 16);
    return 0;
}
