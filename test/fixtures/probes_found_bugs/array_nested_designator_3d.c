// 3 次元配列のネスト指定初期化子 [i][j][k] = val。
// outer_stride/mid_stride から各次元の要素ストライドを求めて平坦化する。
// 修正前: 2 つ目の '[' で構文エラー。
// 期待: exit=43  (7+4+2 の指定要素 + 残り 0; +30)
#include <assert.h>
int main(void) {
  int m[2][3][4] = { [1][2][3] = 7, [0][1][1] = 4, [1][0][0] = 2 };
  assert(m[1][2][3] == 7);   // designated
  assert(m[0][1][1] == 4);
  assert(m[1][0][0] == 2);
  assert(m[0][0][0] == 0);   // 未指定 -> 0
  assert(m[1][1][1] == 0);
  assert(m[0][2][3] == 0);
  return 0;
}
