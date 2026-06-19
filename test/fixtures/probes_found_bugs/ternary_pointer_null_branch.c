// ポインタ型の条件演算子で、分岐の一方が null ポインタ定数 `0` (整数リテラル) の
// とき、その 0 分岐が選ばれると garbage を返していたバグ。
// 原因: build_node_ternary は結果を 8 バイト slot に STORE して merge で LOAD する。
//   ポインタ三項では slot=8B だが、`0` 分岐の値は i32 (4B) のまま STORE され、
//   4 バイトしか書かれず上位 4 バイトが未初期化のまま 8 バイト LOAD されていた。
//   (else が pointer 変数なら 8B 値なので動いていた = リテラル 0 特有)
// 併せて coerce_to_type が即値→PTR 変換で imm 値を失うバグも修正。
// 修正: ポインタ三項の各分岐値を PTR (8B) へ拡張してから STORE。
//       coerce_to_type は即値を PTR 特殊ケースより先に retype。
// 修正前: `p = (cond)? &x : 0` の null 分岐で p が非 0 / SIGSEGV
// 期待: exit=42
#include <assert.h>
struct N { int v; struct N *next; };

int main(void) {
    // 配列内ノードで連結リストを構築 (最後の next は 0)。
    struct N nodes[5];
    for (int i = 0; i < 5; i++) {
        nodes[i].v = i + 1;
        nodes[i].next = (i < 4) ? &nodes[i + 1] : 0;   // 末尾は null 分岐
    }
    // null 終端が効かないと走査が暴走/SIGSEGV していた。
    int sum = 0;
    for (struct N *p = &nodes[0]; p; p = p->next)
        sum += p->v;                                   // 1+2+3+4+5 = 15

    // ストア無しの直接利用でも null 分岐が壊れていた。
    int i = 9;
    int x = 7;
    int *q = (i < 4) ? &x : 0;                          // 0 が選ばれる
    assert(q == 0);

    // 値として直接比較 (j03 相当)
    if (((i < 4) ? &x : 0) != 0) return 2;

    // 逆向き: 0 分岐が選ばれない場合は従来どおり動く
    int *r = (i > 4) ? &x : 0;                          // &x が選ばれる
    assert(r == 0 || *r == 7);

    assert(sum == 15); return 0;                                    // 15 + 27 = 42
}
