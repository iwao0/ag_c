// 続き54: ポインタを非ゼロ整数定数と比較するのを W3022 で警告
// (clang -Wpointer-integer-compare 相当、C11 6.5.16.1)。
//
// equality() で warn_if_pointer_int_compare を呼び、片方が pointer で他方が
// ND_NUM の非ゼロかつ from_pointer_cast=0 なら警告。
//
// 抑制条件:
//   - `p == 0` (NULL ポインタ定数)
//   - `p == (void*)5` (明示キャスト、from_pointer_cast=1)
//   - 両辺ポインタ、両辺整数 (符号比較や signed/unsigned は別経路で扱う)
//
// 本 fixture は合法形のみ。
#include <assert.h>

int main(void) {
    int x = 42;
    int *p = &x;

    /* (a) NULL ポインタ定数 (0) との比較 — 合法 */
    int *q = 0;
    if (p == 0) {}
    if (q != 0) {}
    if (0 == p) {}

    /* (b) ポインタ同士の比較 — 警告なし */
    if (p == q) {}
    if (p != q) {}

    /* (c) 明示キャスト経由 — 警告なし */
    if (p == (int *)0x1000) {}

    /* (d) 整数同士の比較 — 警告なし */
    int a = 5, b = 5;
    if (a == b) {}
    if (a != 100) {}

    assert(*p == 42);
    return 0;
}
