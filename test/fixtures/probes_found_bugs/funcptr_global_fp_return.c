// グローバル関数ポインタ `double (*gops)(double)` を通した間接呼び出しの float/double
// 戻り値が x0 で読まれて壊れていたバグ (`gops(x)`)。
// 原因:
//  (1) global_var_t が funcptr グローバルの戻り型 fp_kind を保持していなかった
//      (ポインタグローバルは fp_kind=NONE。fp_kind は codegen のビットパターン出力用で流用不可)。
//  (2) funcall ノードに戻り型 fp_kind が載らず ir_builder が戻り値を整数 (x0) と判定。
// 修正: global_var_t に pointee_fp_kind を追加し、関数サフィックス付きポインタ宣言子
//      (has_func_suffix) のとき戻り fp_kind を保存。ND_GVAR 解決でノードの pointee_fp_kind
//      へ伝播し、parse_call_postfix が funcall に載せて戻り値を d0 で読む。
// 修正前: 戻り値破損 (x0 を読む)
// 期待: exit=42
#include <assert.h>
double dbl(double x){ return x * 2.0; }
float  mulf(float x){ return x * 3.0f; }
double half(double x){ return x / 2.0; }

double (*gd)(double) = dbl;            // double funcptr グローバル (初期化子付き)
float  (*gf)(float)  = mulf;           // float funcptr グローバル
double (*ga)(double), (*gb)(double);   // 宣言子リスト (両方 funcptr)

int main(void){
    ga = half;
    gb = dbl;
    double r1 = gd(21.0);    // 42.0
    float  r2 = gf(14.0f);   // 42.0f
    double r3 = ga(8.0);     // 4.0
    double r4 = gb(0.5);     // 1.0
    int a = (int)r1, b = (int)r2, c = (int)r3, d = (int)r4;
    assert(a == 42);
    assert(b == 42);
    assert(c == 4);
    assert(d == 1);
    return 0;
}
