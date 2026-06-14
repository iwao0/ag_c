// struct の多次元配列メンバ (`int a[2][2]`) が壊れていたバグ。
// (1) メンバレイアウトが配列をフラット要素数しか保持せず、第1次元の行ストライドが
//     失われていた。メンバアクセス `m.a[i][j]` の第1サブスクリプトが要素サイズで
//     ステップし (本来は行サイズ)、2段目以降が破綻して 139 (SIGSEGV) になっていた。
// (2) ネスト brace 初期化 `{{{1,2},{3,4}}}` / `{{1,2,3,4}}` が E3064 で拒否されていた。
// 修正: メンバの最外次元バイトストライド (outer_stride) を tag_member に保存し、
//       メンバ配列 decay 時に deref_size=行ストライド / inner_deref_size=要素サイズ を
//       設定 (ローカル多次元配列と同じ表現)。初期化子側も行優先でフラット展開。
// 修正前: アクセスで exit=139、ネスト初期化で E3064
// 期待: exit=42
struct M { int a[2][2]; int b[2][3]; };

int sum_all(struct M *p) {           // ポインタ (`->`) 経由の多段アクセス
    int s = 0;
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            s += p->a[i][j];
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 3; j++)
            s += p->b[i][j];
    return s;
}

int main(void) {
    struct M m;
    // `.` 経由の書き込み/読み出し
    int v = 1;
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            m.a[i][j] = v++;          // 1,2,3,4  -> sum 10
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 3; j++)
            m.b[i][j] = 1;            // 6 個      -> sum 6
    int s1 = sum_all(&m);            // 10 + 6 = 16

    // ネスト brace 初期化 (フル多重 brace)
    struct M n = { {{1, 2}, {3, 4}}, {{0, 0, 0}, {0, 0, 0}} };
    int s2 = n.a[0][0] + n.a[0][1] + n.a[1][0] + n.a[1][1];   // 1+2+3+4 = 10

    // フラット brace 初期化 (brace elision)
    struct M k = { {5, 6, 7, 8}, {0, 0, 0, 0, 0, 0} };
    int s3 = k.a[1][0] + k.a[1][1];  // 7 + 8 = 15

    int extra = m.b[1][2];           // = 1
    return s1 + s2 + s3 + extra;     // 16 + 10 + 15 + 1 = 42
}
