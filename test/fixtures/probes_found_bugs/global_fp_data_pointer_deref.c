// グローバルのデータポインタ `double *dp` / `float *fp` の deref (`*dp`) と subscript
// (`dp[i]`) が整数 load で読まれ float/double 値が化けていたバグ。
// 原因: global_var_t がポインタグローバルの pointee fp_kind を保持しておらず
//      (ローカル `double *a` 仮引数/変数は pointee_fp_kind を持つが、グローバルには
//      フィールドが無かった)、ND_GVAR ノードの pointee_fp_kind が NONE のまま deref が
//      整数 load + scvtf になっていた。
// 修正: global_var_t.pointee_fp_kind を追加し、単段データポインタ (ptr_levels==1,
//      非配列, 非関数ポインタ, pointer-to-array でない) のとき pointee fp_kind を保存。
//      ND_GVAR 解決でノードへ伝播し、`*dp` / `dp[i]` が fp load になる。
// 修正前: 値破損
// 期待: exit=42
double dvals[3] = {10.5, 20.5, 11.0};
float  fvals[2] = {1.5f, 2.5f};
double *dp = dvals;       // double データポインタ (配列を指す)
float  *fp = fvals;
double  one = 1.0;
double *dp1 = &one;       // 単一 double を指す

int main(void){
    double s = *dp + dp[1] + dp[2];   // 10.5 + 20.5 + 11.0 = 42.0
    float  f = *fp + fp[1];           // 1.5 + 2.5 = 4.0
    double u = *dp1;                  // 1.0
    int a = (int)s;                   // 42
    int b = (int)f;                   // 4
    int c = (int)u;                   // 1
    return (a == 42 && b == 4 && c == 1) ? 42 : 0;
}
