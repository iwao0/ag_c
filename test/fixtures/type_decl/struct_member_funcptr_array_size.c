// struct メンバが関数ポインタ配列 [2] のとき sizeof = 2 * sizeof(function pointer)
#include <assert.h>
int main(void) {
    struct S { int (*arr[2])(int); };
    assert(sizeof(struct S) == 2 * sizeof(int (*)(int)));
    return 0;
}
