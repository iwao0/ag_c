// 関数ポインタ変数を介した間接呼び出しで float/double 戻り値を読む。
// 旧実装は funcall ノードに戻り型 fp_kind が乗らず、戻り値を整数 x0 で読んでいた
// (FP 戻り値は d0 に返るため化けていた)。`double (*d)(double)` 宣言は `double *d` と
// 同じく pointee_fp_kind=double が立つので、呼び出し時に callee 変数の pointee_fp_kind
// を funcall ノードへ載せて戻り値を d0 から読むようにした。
// 引数側 (fp 値) は元から d0-d7 で渡せていた。
double dsquare(double x){ return x * x; }
float  fadd(float a, float b){ return a + b; }
double dhalf(double x){ return x / 2.0; }
int    icube(int x){ return x * x * x; }   // int 戻りの間接呼び出し (不変確認)

typedef double (*dfn)(double);

double apply(double (*f)(double), double v){ return f(v); }  // 仮引数 funcptr

int main(void){
  int r = 0;

  // double 戻り (直接変数)
  double (*p)(double) = dsquare;
  if ((int)p(7.0) != 49) r |= 1;

  // float 戻り
  float (*fp)(float, float) = fadd;
  if ((int)fp(1.5f, 2.5f) != 4) r |= 2;

  // インライン算術で 2 回呼ぶ
  double (*h)(double) = dhalf;
  if ((int)(h(10.0) + h(20.0)) != 15) r |= 4;

  // typedef した関数ポインタ
  dfn q = dsquare;
  if ((int)q(6.0) != 36) r |= 8;

  // (*p)(args) 明示 deref 形
  if ((int)(*p)(9.0) != 81) r |= 16;

  // 仮引数経由
  if ((int)apply(dsquare, 5.0) != 25) r |= 32;

  // int 戻りの間接呼び出しは従来どおり (x0 から読む)
  int (*ip)(int) = icube;
  if (ip(3) != 27) r |= 64;

  // int 戻りを使った算術も不変
  if (ip(2) * 2 != 16) r |= 128;

  return r == 0 ? 42 : r;
}
