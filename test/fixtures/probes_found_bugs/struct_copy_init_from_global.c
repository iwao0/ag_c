// グローバル構造体からのコピー初期化 `struct S t = g;` が拒否/失敗していたバグ。
// (1) コピー初期化のソースが ND_LVAR のみ対応で、ND_GVAR は E3064 で拒否。
// (2) >8B 構造体の memcpy 代入もソース/デスティネーションが LVAR/DEREF のみ対応で、
//     ND_GVAR が "struct assign src not LVAR/DEREF" で IR build に失敗していた
//     (代入 `t = g` も >8B グローバルで壊れていた)。
// 修正前: E3064 または ir_build_module failed
// 期待: exit=42
#include <assert.h>
struct S { int a; int x[3]; };   // 16B、配列メンバ含む
struct S g = {9, {10, 11, 12}};
int main(void) {
    struct S t = g;                          // グローバルからのコピー初期化
    struct S u;
    u = g;                                   // グローバルからの代入
    // コピー初期化 t と代入 u の全要素を個別に検査 (合計だと取りこぼしを見逃す)。
    assert(t.a == 9);
    assert(t.x[0] == 10);
    assert(t.x[1] == 11);
    assert(t.x[2] == 12);
    assert(u.a == 9);
    assert(u.x[0] == 10);
    assert(u.x[1] == 11);
    assert(u.x[2] == 12);
    return 0;
}
