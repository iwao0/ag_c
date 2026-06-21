/* fp 実引数を整数仮引数へ渡すときの暗黙変換 (C11 6.5.2.2p7, 6.3.1.4)。
 * ag_c は実引数の double/float を fp レジスタ (d0) に置いたまま callee を呼び、
 * callee が整数レジスタ (x0/w0) から読むためゴミ値になっていた。callee の仮引数が
 * 整数スカラのとき、呼び出し側で F2I (fcvtzs) を挿入して切り詰める。
 * long 仮引数は i64 幅で変換しないと大きい値が 32bit で wrap する。 */
#include <assert.h>

int   ei(int x){ return x; }
long  el(long x){ return x; }
short es(short x){ return x; }
char  ec(char x){ return x; }
int   sum2(int a, int b){ return a + b; }
int   mix(int a, double b, long c){ return a + (int)b + (int)c; }

int main(void){
  /* リテラル fp → int 仮引数: 切り捨て (toward zero) */
  assert(ei(7.9) == 7);
  /* 変数 fp → int */
  double d = 5.6;  assert(ei(d) == 5);
  float  f = 2.9f; assert(ei(f) == 2);
  /* 大きい double → long 仮引数: i64 幅が必要 (i32 だと wrap) */
  double big = 5000000000.0;
  assert(el(big) == 5000000000L);
  /* 負の fp → int (toward zero) */
  assert(ei(-3.7) == -3);
  /* sub-int 仮引数 */
  assert(es(100.9) == 100);
  assert(ec(-5.9) == -5);
  /* 混在: int←fp, double←int(既存の順方向), long←fp が同居 */
  assert(mix(1.9, 2, 3.9) == 1 + 2 + 3);
  /* fp 2 個 → int 2 個 */
  assert(sum2(1.5, 2.5) == 3);
  return 0;
}
