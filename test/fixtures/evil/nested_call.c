// 関数呼び出しのネスト
// h(10) = 7、 g(7) = 14、 f(14) = 15
// 期待: exit=15
#include <assert.h>
int f(int x) { return x + 1; }
int g(int x) { return x * 2; }
int h(int x) { return x - 3; }
int main(void) {
    assert(h(10) == 7);
    assert(g(7) == 14);
    assert(f(g(h(10))) == 15);
    return 0;
}
