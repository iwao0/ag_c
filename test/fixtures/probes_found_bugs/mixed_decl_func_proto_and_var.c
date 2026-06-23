// 1 つの宣言で関数プロトタイプと変数を混在させる (C11 6.7.6)。
// `int f(int a), g(int a), a;` が E2006 になっていた。c-testsuite 00121 と同形。
#include <assert.h>

int f(int a), g(int a), a;

int f(int a) { return a; }
int g(int a) { return a; }

int main(void) {
    assert(f(1) - g(1) == 0);
    a = 5;
    assert(a == 5);
    return 0;
}
