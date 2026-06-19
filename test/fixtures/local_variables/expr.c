// ローカル変数の演算と複文
// a=3, b=22 → a + b/2 = 3 + 11 = 14
// 期待: exit=14
#include <assert.h>
int main(void) {
    int a = 3;
    int b = 5 * 6 - 8;
    assert(a + b / 2 == 14);
    return 0;
}
