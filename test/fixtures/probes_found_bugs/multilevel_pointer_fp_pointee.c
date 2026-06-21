/* 多段ポインタを通した浮動小数の load/store。`double **pp; **pp` は fp_kind が
 * 多段ポインタへ伝播せず、float がゴミ・double の書き込みが落ちていた (中核 codegen の穴)。
 * 原因は (1) 宣言時に多段ポインタへ pointee_fp_kind を設定していなかった
 * (decl.c: total_pointer_levels==1 のみ)、(2) build_unary_deref_node / build_subscript_deref
 * が pql を 1 段消費するとき pointee_fp_kind を結果へ引き継いでいなかった。
 * (1) を常に設定し、(2) を多段分岐で伝播して直す。 */
#include <assert.h>

int main(void){
  /* double 多段: read / write */
  double x = 3.5;
  double *p = &x;
  double **pp = &p;
  assert(**pp == 3.5);
  **pp = 9.0;
  assert(x == 9.0);

  /* float 多段: read / write (旧: read がビット列を int 解釈してゴミ) */
  float f = 1.5f;
  float *fp = &f;
  float **fpp = &fp;
  assert(**fpp == 1.5f);
  **fpp = 2.5f;
  assert(f == 2.5f);

  /* 3 段 */
  double ***ppp = &pp;
  assert(***ppp == 9.0);
  ***ppp = 1.25;
  assert(x == 1.25);

  /* 多段ポインタの subscript 経由 (`double **pp = arr; *pp[i]`) */
  double a = 1.0, b = 2.0;
  double *arr[2] = {&a, &b};
  double **q = arr;
  assert(*q[0] == 1.0 && *q[1] == 2.0);
  assert(q[0] == &a);          /* q[i] はポインタ (fp 化されない) */
  *q[1] = 7.0;
  assert(b == 7.0);

  /* pointer-to-array の fp decay 添字 `(*pp)[i]` (read/write, float/double) */
  double da[3] = {10.0, 20.0, 30.0};
  double (*pa)[3] = &da;
  assert((*pa)[1] == 20.0);
  (*pa)[2] = 99.0;
  assert(da[2] == 99.0);
  float ffa[2] = {1.5f, 2.5f};
  float (*pfa)[2] = &ffa;
  assert((*pfa)[0] == 1.5f);

  /* 対照: 単段 double*・int 多段が壊れていないこと */
  double y = 4.0; double *r = &y;
  assert(*r == 4.0); *r = 5.0; assert(y == 5.0);
  int ix = 4; int *ip = &ix; int **ipp = &ip;
  assert(**ipp == 4); **ipp = 6; assert(ix == 6);
  return 0;
}
