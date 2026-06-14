// 重複した指定初期化子 (designated initializer) を ag_c が E3028/E3029 で拒否して
// いたバグ。C11 6.7.9p19 では同一 subobject への複数の初期化子は「後勝ち」で
// 上書きされる (エラーではない)。clang も受理する。
// 修正: 配列・struct の重複 designator チェックを撤廃し、逐次代入を順に発行する
//   ことで最後の指定を有効にする。
// 修正前: コンパイルエラー (E3028 / E3029)
// 期待: exit=42
struct Pt { int x, y; };
struct Box { int a[2]; int z; };

int main(void) {
    // 配列: 後勝ち
    int arr[3] = {[0] = 1, [1] = 2, [0] = 10};   // a[0]=10, a[1]=2, a[2]=0
    if (arr[0] != 10 || arr[1] != 2 || arr[2] != 0) return 1;

    // struct: 同名メンバ後勝ち
    struct Pt p = {.x = 1, .y = 5, .x = 7};       // x=7, y=5
    if (p.x != 7 || p.y != 5) return 2;

    // brace elision 後の集約メンバ上書き (a を埋め切ってから .a で上書き)
    struct Box b = {1, 2, .a = {3, 4}};           // a={3,4}, z=0
    if (b.a[0] != 3 || b.a[1] != 4 || b.z != 0) return 3;

    // 連続上書き
    int c[2] = {[0] = 1, [0] = 2, [0] = 9, [1] = 8};  // c[0]=9, c[1]=8
    if (c[0] != 9 || c[1] != 8) return 4;

    return arr[0] + p.x + b.a[1] + c[0] + 12;     // 10 + 7 + 4 + 9 + 12 = 42
}
