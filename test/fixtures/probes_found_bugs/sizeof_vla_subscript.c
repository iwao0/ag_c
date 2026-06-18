// VLA を subscript した式に sizeof を適用する `sizeof(a[0])` が parse error (E2006) に
// なっていたバグ。`sizeof(a)` (VLA 全体) は動くが、`sizeof(a[0])` / `sizeof(a[i])` が壊れた。
// 原因: parse_sizeof_operand が `sizeof(IDENT)` で IDENT が VLA のとき、次トークンを確認せず
//      ident を消費して `)` を期待していた (非 VLA 配列分岐は peek で `)` を確認していた)。
//      `a[0]` のように postfix が続くと `[` で `)` 期待に失敗していた。
// 修正: VLA 分岐も ident 直後が `)` のときだけ全体サイズ扱いにし、それ以外は式として評価する。
// 修正前: E2006 (コンパイルエラー)
// 期待: exit=42
int main(void){
    int n = 10;
    int a[n];                       // VLA
    int elem = (int)sizeof(a[0]);   // 4
    int whole = (int)sizeof(a);     // 40
    int cnt = whole / elem;         // 10
    // 4 + 10 = 14; さらに a[2] を使う sizeof
    int e2 = (int)sizeof(a[2]);     // 4
    // elem(4) + cnt(10) + e2(4) = 18; + 24 = 42
    return elem + cnt + e2 + 24;
}
