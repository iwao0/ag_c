// グローバルの関数ポインタ配列 `double (*gops[N])(double)` を通した間接呼び出しの
// float/double 戻り値が x0 で読まれて壊れていたバグ (`gops[i](x)`)。
// ローカル funcptr 配列 / スカラ funcptr グローバルは対応済みだが、グローバル funcptr 配列が
// 未対応だった。
// 原因: グローバル funcptr 配列は配列分岐 (ND_ADDR) で解決され、戻り型 fp_kind が node に
//      載らず (gv->fp_kind はポインタ配列なので NONE)、subscript 結果 / funcall が戻り値を
//      x0 で読んでいた。
// 修正: funcptr 配列グローバルの戻り fp_kind を gv->pointee_fp_kind に保存し、配列分岐が
//      ND_ADDR の pointee_fp_kind + base_deref_size(=8) に伝播。build_subscript_deref の
//      「不透明な関数ポインタ要素」分岐に乗り、parse_call_postfix が戻り値を d0 で読む。
// 修正前: 戻り値破損 (x0 を読む)
// 期待: exit=42
// 補足: 要素数 1 の配列 `(*g[1])(double)` は別の既存バグ (paren 内 [1] が is_array を立てず
//      スカラ funcptr として誤登録、int/double 問わず crash) で本修正外。N>=2 を対象とする。
double add1(double x){ return x + 1.0; }
double add2(double x){ return x + 2.0; }
float  mulf(float x){ return x * 3.0f; }
float  mulg(float x){ return x * 2.0f; }
double (*gops[2])(double) = { add1, add2 };
float  (*gfops[2])(float) = { mulf, mulg };

int main(void){
    double r = gops[0](40.0) + gops[1](-1.0);   // 41.0 + 1.0 = 42.0
    int a = (int)r;                              // 42
    int i = 0;
    float fr = gfops[i](14.0f);                  // 42.0f
    int b = (int)fr;                             // 42
    return (a == 42 && b == 42) ? 42 : 0;
}
