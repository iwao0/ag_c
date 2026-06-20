// 多段ポインタ typedef `typedef int **PP; PP p;` が、typedef を 1 段のポインタとしてしか
// 扱わず `**p` が誤 deref で SIGSEGV していた。typedef レコードがポインタ性を bool でしか
// 持たず、段数 (`*` の数) を捨てていたのが原因。
//   修正: typedef_name_t に pointer_levels を追加し、定義時 (toplevel: parser.c /
//   関数内: stmt.c・decl.c) に「基底ポインタ typedef の段数 + 宣言子の prefix `*` 数」を
//   2 段以上のときだけ記録。変数宣言時 (decl.c) に getter で取得して total_pointer_levels /
//   pointer_qual_levels に反映し、直書き `int **p` と同じノード属性 (pql=段数,
//   base_deref_size=要素サイズ) にする。合成 typedef `typedef PI *PP2` も段数を加算。
// 注: グローバル変数での多段ポインタや `(*pp)[i]` 添字は、直書き `int **gp` でも同様に
//   壊れている別の既存バグ (typedef 固有ではない) なのでここでは扱わない。
#include <assert.h>

typedef int   **PP;    // 2 段
typedef int  ***PPP;   // 3 段
typedef int    *PI;
typedef PI     *PP2;   // 合成: PI(1段) + `*` = 2 段

static int deref2(PP p) { return **p; }   // 仮引数経由

int main(void) {
    int x = 42;
    int *xp = &x;
    PP pp = &xp;
    assert(sizeof(pp) == 8);    // ポインタ
    assert(**pp == 42);         // 2 段 deref
    *pp = xp;                   // 1 段 lvalue
    assert(**pp == 42);

    // 関数ローカルで定義した多段 typedef
    typedef int **LP;
    LP lp = &xp;
    assert(**lp == 42);

    // 3 段
    int **b = &xp;
    PPP ppp = &b;
    assert(***ppp == 42);

    // 合成 typedef
    PI a = &x;
    PP2 q = &a;
    assert(**q == 42);

    // 仮引数経由
    assert(deref2(&xp) == 42);

    return 0;
}
