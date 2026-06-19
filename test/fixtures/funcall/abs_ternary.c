// 三項演算子で絶対値関数
// abs_v(-42) + abs_v(7) = 42 + 7 = 49
// 期待: exit=49
#include <assert.h>
int abs_v(int x) { return x < 0 ? -x : x; }
int main(void) {
    assert(abs_v(-42) == 42);
    assert(abs_v(7) == 7);
    return 0;
}
