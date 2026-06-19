// 関数ポインタ配列 int (*ops[N])(...); への代入と呼び出し
// 修正前: 配列要素サイズが 8 ではなく 4 として扱われ、ops[i] への代入が
//         32 bit 切り詰めになり、呼び出しで SEGV (exit 139)
// 期待: exit=20 ((10+3) + (10-3))
#include <assert.h>
int add(int a, int b) { return a + b; }
int sub(int a, int b) { return a - b; }
int main(void) {
    int (*ops[2])(int, int);
    ops[0] = add;
    ops[1] = sub;
    assert(ops[0](10, 3) == 13);
    assert(ops[1](10, 3) == 7);
    return 0;
}
