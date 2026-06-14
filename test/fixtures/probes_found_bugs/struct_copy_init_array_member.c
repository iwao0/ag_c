// 配列メンバを含む構造体のコピー初期化 `struct S t = s;` が壊れていたバグ。
// build_struct_copy_chain_from_source が配列メンバを 1 要素分 (type_size は
// 要素サイズ) のスカラ assign で済ませており、先頭要素しかコピーされず
// 残りはゴミになっていた (代入 `t = s;` 経路は別処理で正常だった)。
// 修正: 配列メンバ (array_len>0) は全体サイズ type_size*array_len をバイトコピー。
// 修正前: exit=0 (t.x[1]/t.x[2] がゴミ)
// 期待: exit=42
struct S { int a; int x[3]; };
int main(void) {
    struct S s = {9, {10, 20, 30}};
    struct S t = s;                 // コピー初期化
    int sum = t.a + t.x[0] + t.x[1] + t.x[2];  // 9+10+20+30 = 69
    return (sum == 69 && t.x[2] == 30) ? 42 : 0;
}
