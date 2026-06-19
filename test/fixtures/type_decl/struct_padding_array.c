// padding 込みの struct 配列の書き込み・読み出し
// 期待: exit=3
#include <assert.h>
int main(void) {
    struct S { char c; int x; };
    struct S a[2];
    a[0].x = 3;
    a[1].c = 9;
    assert(a[0].x == 3);
    return 0;
}
