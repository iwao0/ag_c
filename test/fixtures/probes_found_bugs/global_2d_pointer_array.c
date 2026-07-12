/* グローバルの 2 次元 (以上の) ポインタ配列 `int *t[2][2]` / `int (*t[2][2])(void)` /
 * `char *names[2][2]` が `t[i][j]` で SIGSEGV だった (非ポインタ `int t[2][2]` は動作)。
 * 3 つの修正:
 *   (1) apply_global_multidim_strides の `!head.is_ptr` ゲートがポインタ要素配列を除外し
 *       ストライドが立たず、`t[i]` が「ポインタ値として load → [j] で deref」と誤計算
 *       (SIGSEGV)。ゲートを外し elem_size=8 (ポインタ) で stride を立てる。
 *   (2) build_subscript_deref の pointee_is_scalar_ptr が中間次元でも load を起こしていた。
 *       最終次元 (inner_ds==0) のみ load し、中間は伝播 + 要素 pointee サイズを base_deref_size
 *       で carry する。最終要素の load 幅もこの carry 値を使う (2D 以上は base が ND_ADDR で
 *       なく中間 ND_DEREF なので gv を引けない)。
 *   (3) 括弧内配列 `(*t[2][2])` の paren_array_mul は積(4)のみで個別 dims を捨てていた。
 *       ps_parse_array_suffixes_capture_dims で {2,2} を捕捉し多次元ストライドを設定。 */
#include <assert.h>

int w = 1, x = 2, y = 3, z = 4;
int *dp[2][2] = { {&w, &x}, {&y, &z} };          /* 2D data ptr array */

int v0=10,v1=11,v2=12,v3=13,v4=14,v5=15,v6=16,v7=17;
int *tp[2][2][2] = { {{&v0,&v1},{&v2,&v3}}, {{&v4,&v5},{&v6,&v7}} };  /* 3D */

char *names[2][2] = { {"ab", "cd"}, {"ef", "gh"} };  /* char* 2D */

int fa(void){return 1;} int fb(void){return 2;} int fc(void){return 3;} int fd(void){return 4;}
int (*fp[2][2])(void) = { {fa, fb}, {fc, fd} };  /* 2D function ptr array */

int main(void) {
  /* 2D data ptr: 値読み出し */
  assert(*dp[0][0] == 1 && *dp[0][1] == 2);
  assert(*dp[1][0] == 3 && *dp[1][1] == 4);
  /* 2D data ptr: 要素経由の代入 */
  *dp[1][0] = 99;
  assert(y == 99);

  /* 3D */
  assert(*tp[0][0][0] == 10 && *tp[1][0][1] == 15 && *tp[1][1][1] == 17);

  /* char* 2D (文字列ポインタ) */
  assert(names[0][0][0] == 'a' && names[0][1][1] == 'd');
  assert(names[1][0][0] == 'e' && names[1][1][1] == 'h');

  /* 2D function ptr: 直接呼び出し */
  assert(fp[0][0]() == 1 && fp[0][1]() == 2);
  assert(fp[1][0]() == 3 && fp[1][1]() == 4);
  /* 値で受けてから呼ぶ */
  int (*g)(void) = fp[1][1];
  assert(g() == 4);

  return 0;
}
