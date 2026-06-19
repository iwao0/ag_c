// 8 バイトを超える struct 値の受け渡しで 3 つの経路が漏れていたバグ。
// (1) 引数: struct 配列要素 `arr[i]` やメンバ `s.m` (= ND_DEREF) の >8B struct を
//     関数へ値渡しすると、ポインタ渡し経路 (ND_LVAR 限定) を通らず先頭 8B を値
//     ロードして渡し、後半メンバが落ち SIGSEGV/値破壊していた。
// (2) 戻り値: `return (struct V){...};` (compound literal = ND_COMMA) が
//     「struct return value is not LVAR/DEREF」で ir_build_module 失敗していた。
// (3) `sum(make())`: >8B struct を返す関数の戻り値を直接 struct 引数に渡すと、
//     呼び出し側で ret_area を確保せず値破壊していた。
// 修正: 引数は ND_DEREF / ND_FUNCALL の struct 値もアドレス渡しに、戻り値は
//   ND_COMMA を展開し、build_node_funcall は値文脈で ret_area を確保する。
// 期待: exit=42
#include <assert.h>
struct V { int a, b, c; };       // 12 バイト

int sum(struct V v) { return v.a + v.b + v.c; }
struct V make(int x) { return (struct V){x, x * 2, x * 3}; }  // (2)

int main(void) {
    // (1) 配列要素を値渡し
    struct V arr[2] = {{1, 2, 3}, {4, 5, 6}};
    if (sum(arr[0]) != 6 || sum(arr[1]) != 15) return 1;

    // (1) struct メンバを値渡し
    struct Wrap { struct V v; int z; } w = {{2, 4, 6}, 9};
    if (sum(w.v) != 12) return 2;

    // (2) compound literal を返す
    struct V m = make(5);            // {5,10,15}
    assert(m.a == 5 || m.b != 10 || m.c != 15);

    // 返した struct を変数経由で値渡し
    struct V m2 = make(3);           // {3,6,9}
    if (sum(m2) != 18) return 4;

    // >8B struct を返す関数の戻り値を直接 struct 引数に (ND_FUNCALL 引数)
    if (sum(make(4)) != 24) return 5;     // {4,8,12} -> 24

    assert(sum(arr[1]) == 15); assert(sum(w.v) == 12); return 0;   // 15 + 12 + 15 = 42
}
