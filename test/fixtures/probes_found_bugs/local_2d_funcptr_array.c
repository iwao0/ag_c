/* ローカルの 2 次元 (以上の) 関数ポインタ配列 `int (*t[2][2])(void)`。
 * 以前は (1) ネスト brace init `{{a,b},{c,d}}` が E3064、(2) 要素を個別代入しても `t[i][j]()` が
 * SIGSEGV だった (1 次元 `int(*ops[N])(int)` は動作、グローバル版は 320e0ff で修正済み)。
 * 原因: funcptr 配列の局所登録 (decl.c:3185) が括弧内 `[N][M]` を inner_array_mul の積に潰し
 * flat 1 次元配列として登録、多次元ストライドを立てていなかった。括弧内個別次元 (g_inner_array_dims)
 * を捕捉して outer_stride/mid_stride (要素 8B funcptr) を設定。ストライドが立つことで 2D 配列と
 * 正しく認識され、ネスト brace init も通るようになった (E3064 も解消)。 */
#include <assert.h>

int a(void){return 1;} int b(void){return 2;} int c(void){return 3;} int d(void){return 4;}
int add(int x){return x + 1;} int mul(int x){return x * 2;}
int f0(void){return 0;} int f1(void){return 1;} int f2(void){return 2;} int f3(void){return 3;}
int f4(void){return 4;} int f5(void){return 5;} int f6(void){return 6;} int f7(void){return 7;}

int main(void) {
  /* ネスト brace init + 直接呼び出し */
  int (*t[2][2])(void) = { {a, b}, {c, d} };
  assert(t[0][0]() == 1 && t[0][1]() == 2);
  assert(t[1][0]() == 3 && t[1][1]() == 4);

  /* 値で受けてから呼ぶ */
  int (*g)(void) = t[1][0];
  assert(g() == 3);

  /* 個別代入 (brace init を使わない経路) */
  int (*u[2][2])(void);
  u[0][0] = a; u[0][1] = b; u[1][0] = c; u[1][1] = d;
  assert(u[0][0]() + u[1][1]() * 10 == 41);

  /* 引数つき funcptr の 2D 配列 */
  int (*ops[1][2])(int) = { {add, mul} };
  assert(ops[0][0](10) == 11 && ops[0][1](10) == 20);

  /* 3D */
  int (*w[2][2][2])(void) = { {{f0, f1}, {f2, f3}}, {{f4, f5}, {f6, f7}} };
  assert(w[0][0][0]() == 0 && w[1][0][1]() == 5 && w[1][1][1]() == 7);

  /* 回帰防止: 1D funcptr 配列 */
  int (*v[2])(void) = { a, d };
  assert(v[0]() == 1 && v[1]() == 4);

  return 0;
}
