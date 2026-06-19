// 冗長な括弧付きの関数名定義 `int (f)(int x)`
// 期待: exit=42
#include <assert.h>
int (f)(int x) { return x; }
int main(void) {
    assert(f(42) == 42);
    return 0;
}
