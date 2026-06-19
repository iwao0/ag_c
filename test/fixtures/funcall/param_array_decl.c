// 配列仮引数の宣言
// 期待: exit=5
#include <assert.h>
int f(int a[], int n) { return n; }
int main(void) {
    assert(f(0, 5) == 5);
    return 0;
}
