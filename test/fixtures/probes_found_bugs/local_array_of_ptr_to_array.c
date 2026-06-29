// ローカル array-of-pointer-to-array `int (*p[M])[N]` の direct subscript
// (`p[i][j][k]`) が、配列スロットのポインタ値ではなくスロット自身のアドレスを
// 基点にして誤値になっていた。
//
// struct メンバ版は ptr_array_pointee_bytes を持っていたが、ローカル lvar 側には
// 同じ型メタデータがなく、`p[0]` の後続 subscript が余分/不足した load 経路に落ちた。
#include <assert.h>

int main(void) {
  int a[2][3] = {{1, 2, 3}, {4, 5, 6}};
  int b[2][3] = {{7, 8, 9}, {10, 11, 12}};

  int (*p[2])[3] = {a, b};
  assert(p[0][0][0] == 1);
  assert(p[0][1][2] == 6);
  assert(p[1][0][1] == 8);
  assert(p[1][1][2] == 12);

  /* explicit deref 形も同じ型情報で動く */
  assert((*p[0])[2] == 3);
  assert((*(p[1] + 1))[0] == 10);

  p[0][1][1] = 42;
  assert(a[1][1] == 42);

  int (*m[2][2])[3] = {{a, b}, {b, a}};
  assert(m[0][0][1][0] == 4);
  assert(m[0][1][0][2] == 9);
  assert(m[1][0][1][1] == 11);
  assert(m[1][1][0][1] == 2);

  m[1][1][1][2] = 77;
  assert(a[1][2] == 77);
  return 0;
}
