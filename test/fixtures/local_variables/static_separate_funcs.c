// 別関数の static 変数は独立 (名前空間が分離) する
// f() は呼ぶたび 1→2、g() は 100→101 と独立に進む
#include <assert.h>
int f(void) { static int n = 0; return ++n; }
int g(void) { static int n = 99; return ++n; }
int main(void) {
    int f1 = f();
    int g1 = g();
    int f2 = f();
    int g2 = g();
    assert(f1 == 1);
    assert(g1 == 100);
    assert(f2 == 2);
    assert(g2 == 101);
    return 0;
}
