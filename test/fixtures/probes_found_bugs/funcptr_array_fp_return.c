// 関数ポインタ配列の要素 `double (*ops[N])(double)` を通した間接呼び出しの
// float/double 戻り値が x0 で読まれて壊れていたバグ。
// 原因:
//  (1) build_subscript_deref が funcptr 配列の subscript 結果 `ops[i]` を「double 値」
//      扱い (base.fp_kind=double) にしていたため要素を fp としてロードしていた。
//  (2) funcall ノードに戻り型 fp_kind が載らず ir_builder が戻り値を整数 (x0) と判定。
// 修正: subscript の else 分岐で `inner_ds==0 && bds>0` (= 不透明な関数ポインタ要素) を
//      検出し is_pointer + pointee_fp_kind(戻り fp) を立て、parse_call_postfix は callee
//      全般の pointee_fp_kind を funcall に載せて戻り値を d0 で読む。
// 修正前: 戻り値破損 (x0 を読む)
// 期待: exit=42
#include <assert.h>
double add1(double x){ return x + 1.0; }
double add2(double x){ return x + 2.0; }
float  mulf(float x){ return x * 3.0f; }

int main(void){
    // double を返す関数ポインタ配列
    double (*ops[2])(double) = { add1, add2 };
    double r = ops[0](40.0) + ops[1](-1.0);   // 41.0 + 1.0 = 42.0
    int a = (int)r;                            // 42

    // 添字変数経由 + float を返す関数ポインタ配列
    float (*fops[1])(float) = { mulf };
    int i = 0;
    float fr = fops[i](14.0f);                 // 42.0f
    int b = (int)fr;                           // 42

    assert(a == 42);
    assert(b == 42);
    return 0;
}
