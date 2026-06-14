// グローバルの入れ子 struct メンバの初期化が壊れていたバグ。
// グローバル data 出力 (emit_global_struct_init) がトップレベルメンバのみ走査し、
// 入れ子 struct メンバ (`struct Out{struct In i; int z;}` の i) を 1 つの .quad
// (= struct In のサイズ 8 バイト) として出力していた。フラット初期化値が 1 個しか
// 消費されず、`g={1,2,3}` が i.p=1, i.q=0, z=2 に化けていた (.quad 1; .long 2)。
// 修正: メンバが struct のとき再帰してフラット展開する (.long 1; .long 2; .long 3)。
// 修正前: i.q が 0 に化ける
// 期待: exit=42
struct In  { int p, q; };
struct Out { struct In i; int z; };
struct Two { struct In a; struct In b; };
struct WithArr { int head; struct In mid; int tail; };

// (1) 入れ子 struct の brace 省略
struct Out g1 = {1, 2, 3};            // i.p=1, i.q=2, z=3
// (2) 明示 brace
struct Two g2 = {{4, 5}, {6, 7}};     // a={4,5}, b={6,7}
// (3) 入れ子 struct を挟んだ混在レイアウト
struct WithArr g3 = {8, 9, 10, 11};   // head=8, mid={9,10}, tail=11

int main(void) {
    if (g1.i.p != 1 || g1.i.q != 2 || g1.z != 3) return 1;
    if (g2.a.p != 4 || g2.a.q != 5 || g2.b.p != 6 || g2.b.q != 7) return 2;
    if (g3.head != 8 || g3.mid.p != 9 || g3.mid.q != 10 || g3.tail != 11) return 3;

    return g1.i.q + g2.b.q + g3.mid.q + g3.tail + g2.a.p + 8;  // 2+7+10+11+4+8 = 42
}
