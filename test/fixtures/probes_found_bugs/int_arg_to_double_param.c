// 整数リテラル / 整数値を double 仮引数へ渡す (暗黙の int → double 変換)
// 修正前: IR builder の ND_FUNCALL は実引数を build_expr した結果を
// そのまま cargs[] に格納するだけで、callee のプロトタイプ仮引数が double
// でも変換しなかった。結果、整数値が整数レジスタ (x0..x7) で渡されるが
// 関数側は FP レジスタ (d0..d7) から読むため、garbage を加算 → (int) で 0。
// IR loweringがcanonical parameter typeを分類し、I2Fキャストを挿入する。
#include <assert.h>
int sum(double a, double b, double c, double d,
        double e, double f, double g, double h,
        double i, double j) {
  return (int)(a+b+c+d+e+f+g+h+i+j);
}
int main(void) {
  assert(sum(1,2,3,4,5,6,7,8,9,10) == 55); return 0; // 55
}
// 期待: 55
