// 宣言指定子の順序自由 (C11 6.7p1)、storage class 重複検出 (C11 6.7.1p2)、
// グローバル変数の tentative def merge (C11 6.9.2)、qualifier 重複 (C11 6.7.3p5)。
//
// 修正前:
// - `int static x` (型 → storage class 順) が「変数名が必要」E3016 で拒否されていた
//   (型指定子ループが storage class を消費せず、デクラレータパースが static を name と
//   誤判定したため)。C11 では declaration-specifiers の順序は任意。
// - グローバル `int g=1; int g=2;` の重複定義が silently 通過し、後段でアセンブラが
//   "duplicate symbol" を出すまで気づけなかった。
// - グローバル `int g; double g;` の型不一致が silently 通過。
// - `int static static x` (interleaved 重複) 等の storage class 多重指定が見逃されていた。
//
// 本 fixture は「合法形」の回帰確認。エラー形は probe (一時) で確認。
#include <assert.h>

/* (a) declaration-specifier 順序自由 */
const static int A_a = 5;
int static const A_b = 6;
unsigned static int A_c = 7;
static const int A_d = 8;

/* (b) qualifier 重複 (C11 6.7.3p5: const const = const) */
const const int B_a = 10;
volatile volatile int B_b;

/* (c) tentative def 同型 merge (両方初期化子なし — 1 つの definition と等価) */
int C_g;
int C_g;

/* (d) const + volatile 組合せ */
const volatile int D_x = 99;
volatile const int D_y = 88;

int main(void) {
    assert(A_a == 5);
    assert(A_b == 6);
    assert(A_c == 7);
    assert(A_d == 8);
    assert(B_a == 10);
    /* B_b は volatile int、デフォルト 0 */
    assert(B_b == 0);
    /* tentative def: C_g は 0 (BSS) */
    assert(C_g == 0);
    assert(D_x == 99);
    assert(D_y == 88);
    return 0;
}
