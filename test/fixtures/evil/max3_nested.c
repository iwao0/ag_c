// 3 値の最大 (max のネスト)
// 期待: exit=42
#include <assert.h>
int max(int a, int b) { return a > b ? a : b; }
int max3(int a, int b, int c) { return max(max(a, b), c); }
int main(void) {
    assert(max3(17, 42, 23) == 42);
    return 0;
}
