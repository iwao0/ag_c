// 浮動小数点戻り型の関数が整数値を return すると暗黙変換されないバグ。
// (1) 関数定義ノードに戻り型の fp_kind が設定されず、ir_type_from_node が
//     IR_TY_I32 を返すため callee が整数レジスタ x0 で返していた
//     (caller は fp レジスタ d0 から読むため値が化ける)。
// (2) return 文が戻り値を関数の戻り型へ変換 (I2F) していなかった。
// `return 7.0` (既に double) だけは偶然動いていた。
// 修正前: exit=0
// 期待: exit=42  (sumi(20,22)=42 を double で返し int へ)
#include <assert.h>
double sumi(int a, int b) { return a + b; }   // int 式 -> double 戻り
double seven(void) { return 7; }              // int リテラル -> double 戻り
int main(void) {
    double s = sumi(20, 22);                  // 42.0
    int t = (int)seven();                     // 7
    assert(s == 42.0); assert(t == 7); return 0;
}
