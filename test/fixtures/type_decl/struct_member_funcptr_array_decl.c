// struct メンバに関数ポインタ配列
// 期待: exit=0
#include <assert.h>
int main(void) {
    struct S { int (*arr[2])(int); };
    struct S s;
    return 0;
}
