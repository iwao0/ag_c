// 複数の inline 関数の共存: 各呼び出しを個別に検査
#include <assert.h>
inline int add(int a, int b) { return a + b; }
inline int mul(int a, int b) { return a * b; }
int main(void) {
    assert(add(3, 4) == 7);
    assert(mul(2, 5) == 10);
    return 0;
}
