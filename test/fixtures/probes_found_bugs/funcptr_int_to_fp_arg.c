/* 関数ポインタ経由呼び出しでの int→fp 引数昇格 (C11 6.5.2.2)。
 * `double (*fp)(double); fp(3)` は 3 を double に昇格して d0 で渡すべきだが、
 * 直接呼び出し (ir_builder が coerce) と違い間接呼び出しは funcptr 変数が仮引数型を
 * 持たず、int 3 が整数レジスタのまま渡って d0 にゴミが入っていた。宣言時に
 * skip_func_params で各仮引数の fp 種別を funcptr_param_fp_mask に記録し、
 * parse_call_postfix で fp 仮引数の実引数を wrap_to_fp (ND_INT_TO_FP) でラップする。
 * （typedef 経由 funcptr `typedef double (*F)(double)` は typedef 側に mask を持たせる
 *   別経路が必要で本コミットでは未対応。直書き funcptr のみ。） */
#include <assert.h>

static double did(double x){ return x; }
static float  fid(float x){ return x; }
static double dadd(double a, double b){ return a + b; }
static double mix(int a, double b, int c){ return a + b + c; }
static int    ieat(int x){ return x; }

int main(void){
  double (*fp)(double) = did;
  assert(fp(3) == 3.0);            /* int -> double */
  assert(fp(7) == 7.0);
  assert(fp(2.5) == 2.5);          /* double 実引数は no-op */

  float (*ff)(float) = fid;
  assert(ff(5) == 5.0f);           /* int -> float */

  double (*fa)(double,double) = dadd;
  assert(fa(1, 2) == 3.0);         /* 両仮引数 fp */

  double (*fm)(int,double,int) = mix;
  assert(fm(1, 2, 3) == 6.0);      /* 混在: b だけ昇格 */

  assert((*fp)(9) == 9.0);         /* (*fp)() 構文 */

  int (*ip)(int) = ieat;
  assert(ip(42) == 42);            /* 整数仮引数 funcptr は変換しない (対照) */
  return 0;
}
