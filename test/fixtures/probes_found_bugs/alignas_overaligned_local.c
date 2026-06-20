// C11 _Alignas(>16) のローカル変数が実際には過剰整列されていなかった。x29 (フレーム
// 基底) は 16 整列のみなので `x29 + 固定オフセット` では >16 整列にできない。過剰整列
// ローカルだけ予備領域を確保し、実行時にアドレスを丸める (IR_ALIGN_PTR)。
// 併せて `_Alignas(N) struct ...` のローカル宣言がパースできなかった (_Alignas の (N) を
// 読み飛ばせず struct 検出前に E3015) のも修正。
#include <assert.h>

struct S { int x; char c; };

int main(void) {
    _Alignas(32) int x = 7;
    assert(((unsigned long)&x % 32) == 0);
    assert(x == 7);

    _Alignas(64) int a[4];
    assert(((unsigned long)&a % 64) == 0);
    for (int i = 0; i < 4; i++) a[i] = i * 10;
    assert(a[0] == 0 && a[3] == 30);

    /* 複数の過剰整列ローカルが独立に整列される */
    _Alignas(32) long y = 5;
    _Alignas(128) char z = 9;
    assert(((unsigned long)&y % 32) == 0);
    assert(((unsigned long)&z % 128) == 0);
    assert(y == 5 && z == 9);

    /* _Alignas(N) struct (tagged / 無名) のローカル */
    _Alignas(32) struct S s;
    s.x = 11; s.c = 3;
    assert(((unsigned long)&s % 32) == 0);
    assert(s.x == 11 && s.c == 3);

    _Alignas(32) struct { int a, b; } anon;
    anon.a = 1; anon.b = 2;
    assert(((unsigned long)&anon % 32) == 0);
    assert(anon.a == 1 && anon.b == 2);

    /* 過剰整列ローカルと VLA が同一関数に共存 */
    int n = 4;
    int v[n];
    for (int i = 0; i < n; i++) v[i] = i;
    _Alignas(32) int w = 42;
    assert(((unsigned long)&w % 32) == 0);
    assert(v[3] == 3 && w == 42);

    /* _Alignas(16) と通常ローカルは従来どおり */
    _Alignas(16) int p = 1;
    int q = 2;
    assert(((unsigned long)&p % 16) == 0);
    assert(p + q == 3);
    return 0;
}
