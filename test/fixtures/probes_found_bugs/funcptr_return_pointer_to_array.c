/* 関数ポインタが配列へのポインタを返す `int (*(*fp)())[N]` の間接呼び出し。
 * 直接関数 `int (*f())[N]` は func_return_pointer_to_array で修正済みだったが、
 * ND_FUNCALL.callee 経由では戻り値の pointee 配列次元/要素サイズを持っていなかったため、
 * `fp()[i][j]` が E3064 または誤スケールになっていた。double 要素では callee の
 * pointee_fp_kind を「戻り値が double」と誤解し、ポインタ戻り値を d0 から読んで SIGSEGV
 * していた。typedef・global・struct メンバ経由も同じメタデータを伝播する。 */
#include <assert.h>

static int m[3][3] = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
static double dm[2][2] = {{1.5, 2.5}, {3.5, 4.5}};

int (*getrow(void))[3] { return m; }
int (*getrow_from(int s))[3] { return m + s; }
double (*getd(void))[2] { return dm; }

typedef int (*(*GetRow)(void))[3];
typedef int (*(*GetRowFrom)(int))[3];
typedef double (*(*GetD)(void))[2];

struct Ops {
  GetRow gr;
  GetRowFrom grf;
  GetD gd;
};

GetRow ggr = getrow;
GetD ggd = getd;

int main(void) {
  int (*(*direct)(void))[3] = getrow;
  double (*(*ddirect)(void))[2] = getd;
  GetRow gr = getrow;
  GetRowFrom grf = getrow_from;
  GetD gd = getd;
  struct Ops ops = {getrow, getrow_from, getd};

  assert(direct()[1][2] == 6);
  assert((*direct())[2] == 3);
  assert((*(direct() + 1))[0] == 4);

  assert(gr()[1][2] == 6);
  assert((*gr())[2] == 3);
  gr()[0][1] = 99;
  assert(m[0][1] == 99);
  m[0][1] = 2;
  assert((*(gr() + 1))[0] == 4);
  assert(grf(1)[0][2] == 6);

  assert(ggr()[2][1] == 8);
  assert(ops.gr()[1][1] == 5);
  assert(ops.grf(2)[0][0] == 7);

  assert(ddirect()[1][0] == 3.5);
  assert((*ddirect())[1] == 2.5);
  assert(gd()[1][0] == 3.5);
  assert((*gd())[1] == 2.5);
  assert(ggd()[1][1] == 4.5);
  assert(ops.gd()[0][1] == 2.5);
  return 0;
}
