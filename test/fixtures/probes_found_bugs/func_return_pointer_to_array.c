/* 配列へのポインタを返す関数 `int (*f())[N]` の戻り値を直接 subscript / deref する
 * `f()[i][j]` / `(*f())[j]` / `(*(f()+k))[j]` (C11 6.5.2.2)。parse_func_declarator が pointee の
 * 配列次元 `[N]` を読み飛ばしていたため、call ノードに行ストライド (N*elem) 情報が無く base
 * 要素サイズ (4) で誤スケールして SIGSEGV していた (型付き変数へ代入 `int(*p)[N]=f()` は動作)。
 * 修正: 関数の canonical 戻り値型を pointer(array(...)) として保持し、call / subscript /
 * unary deref が同じ再帰型から行ストライドと要素サイズを導出する。 */
#include <assert.h>

static int m[3][3] = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
static double dm[2][2] = {{1.5, 2.5}, {3.5, 4.5}};

int (*getrow(void))[3] { return m; }
int (*getrow_from(int s))[3] { return m + s; }     /* 引数あり */
double (*getd(void))[2] { return dm; }

int main(void) {
  /* subscript 形 f()[i][j] (read / write) */
  assert(getrow()[1][2] == 6);
  assert(getrow()[0][0] == 1 && getrow()[2][1] == 8);
  getrow()[0][1] = 99;
  assert(m[0][1] == 99);
  m[0][1] = 2;

  /* 明示 deref (*f())[j] */
  assert((*getrow())[2] == 3);

  /* ポインタ算術 + deref (*(f()+k))[j] と f()[k][j] の一致 */
  assert((*(getrow() + 1))[0] == 4);
  assert(getrow()[1][0] == 4);

  /* ポインタ値・型付き変数代入 (非回帰) */
  assert(getrow() == m);
  int (*p)[3] = getrow();
  assert(p[2][2] == 9);

  /* 引数つき */
  assert(getrow_from(1)[0][2] == 6);   /* (m+1)[0][2] = m[1][2] */

  /* fp 要素 */
  assert(getd()[1][0] == 3.5);
  assert((*getd())[1] == 2.5);
  return 0;
}
