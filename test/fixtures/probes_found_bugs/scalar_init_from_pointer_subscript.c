// 単段ポインタの subscript `p[i]` からスカラ変数を初期化すると、`p[i]` が誤って
// 「ポインタ型」と判定され E3064 (スカラ変数をポインタ型で初期化できません) で
// 拒否されていたバグ (セッション前から存在)。
// 原因: build_subscript_deref が pointer_qual_levels>=1 のベースの subscript 結果を
//   常に is_pointer=1 にしていた。`int *p` (pql=1, base_deref_size=0) は p[i] が
//   スカラなのに pointer 扱いされ、`int x = p[i];` の初期化チェックで弾かれた。
//   `int *arr[N]` / `int **pp` (base_deref_size>0) は要素がポインタなので結果も
//   pointer で正しい。
// 修正: base_deref_size>0 (要素自体がポインタ) のときだけ subscript 結果を
//   pointer にする。単段スカラポインタ (long*/char*/int*) はスカラ要素を返す。
// 修正前: `int x = p[0];` が E3064 でコンパイルエラー
// 期待: exit=42
int main(void) {
    int arr[3] = {10, 20, 12};
    int *p = arr;
    int a = p[0];                 // 単段 int* subscript → スカラ (修正前 E3064)
    int b = p[2];

    long la[2] = {100, 5};
    long *lp = la;
    long c = lp[0];               // 単段 long* subscript → スカラ

    char buf[3] = {7, 0, 0};
    char *cp = buf;
    char d = cp[0];               // 単段 char* subscript → スカラ

    // ポインタ配列は要素がポインタなので従来どおり (回帰ガード)
    int x = 5, y = 9;
    int *pa[2] = {&x, &y};
    int viaArr = *pa[0] + *pa[1];  // pa[i] は int* のまま

    // a=10, b=12, c=100, d=7, viaArr=14
    return a + b + (int)c - d - viaArr + (42 - 101);  // 10+12+100-7-14-59 = 42
}
