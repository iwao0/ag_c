/* 配列へのポインタを返す関数 `int (*f())[N][M]` の戻り値を直接 subscript / deref する
 * `f()[i][j][k]` / `(*f())[j][k]` / `(*(f()+i))[j][k]` (C11 6.5.2.2)。
 * `int (*f())[N]` では先頭次元 N だけで行ストライドを復元できたが、2D pointee では
 * 呼び出し結果 `f()[i]` の stride が N*M*elem、次段が M*elem になるため第2次元 M も必要。
 * 修正: parse_func_declarator が `[N][M]` の第2次元を ctx に記録し、ND_FUNCALL の deref_size と
 * subscript / `*f()` の inner_deref_size へ N*M*elem / M*elem / elem を carry する。 */
#include <assert.h>

static int m[2][3][4] = {
  {{1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10, 11, 12}},
  {{13, 14, 15, 16}, {17, 18, 19, 20}, {21, 22, 23, 24}},
};

static double dm[2][2][3] = {
  {{1.5, 2.5, 3.5}, {4.5, 5.5, 6.5}},
  {{7.5, 8.5, 9.5}, {10.5, 11.5, 12.5}},
};

int (*get2d(void))[3][4] { return m; }
int (*get2d_from(int s))[3][4] { return m + s; }
double (*getd2d(void))[2][3] { return dm; }

int main(void) {
  /* subscript 形 f()[i][j][k] (read / write) */
  assert(get2d()[0][1][2] == 7);
  assert(get2d()[1][2][3] == 24);
  get2d()[1][1][1] = 77;
  assert(m[1][1][1] == 77);
  m[1][1][1] = 18;

  /* 明示 deref (*f())[j][k] */
  assert((*get2d())[2][1] == 10);

  /* ポインタ算術 + deref (*(f()+i))[j][k] と f()[i][j][k] の一致 */
  assert((*(get2d() + 1))[0][0] == 13);
  assert(get2d()[1][0][0] == 13);

  /* ポインタ値・型付き変数代入 (非回帰) */
  assert(get2d() == m);
  int (*p)[3][4] = get2d();
  assert(p[1][2][0] == 21);

  /* 引数つき */
  assert(get2d_from(1)[0][2][0] == 21);

  /* fp 要素 */
  assert(getd2d()[1][0][2] == 9.5);
  assert((*getd2d())[1][1] == 5.5);
  return 0;
}
