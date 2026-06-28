/* 関数ポインタが 2D 配列へのポインタを返す `int (*(*fp)())[N][M]` の間接呼び出し。
 * 1D pointee の `int (*(*fp)())[N]` では first_dim と elem_size だけで stride を復元できたが、
 * 2D pointee では `fp()[i]` が N*M*elem、次段が M*elem、最終段が elem でスケールするため
 * second_dim も必要。直書き global/struct member では trailing `[N][M]` をオブジェクト自身の
 * 配列として登録してしまう経路もあり、SIGBUS になっていた。 */
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

typedef int (*(*Get2D)(void))[3][4];
typedef int (*(*Get2DFrom)(int))[3][4];
typedef double (*(*GetD2D)(void))[2][3];

struct TypedefOps {
  Get2D gi;
  Get2DFrom gif;
  GetD2D gd;
};

struct DirectOps {
  int (*(*gi)(void))[3][4];
  int (*(*gif)(int))[3][4];
};

Get2D ggi = get2d;
GetD2D ggd = getd2d;
int (*(*direct_global)(void))[3][4] = get2d;
int (*(*direct_global_from)(int))[3][4] = get2d_from;

int main(void) {
  int (*(*direct)(void))[3][4] = get2d;
  int (*(*direct_from)(int))[3][4] = get2d_from;
  double (*(*ddirect)(void))[2][3] = getd2d;
  Get2D gi = get2d;
  Get2DFrom gif = get2d_from;
  GetD2D gd = getd2d;
  struct TypedefOps ops = {get2d, get2d_from, getd2d};
  struct DirectOps dops = {get2d, get2d_from};

  /* 直書き local: read / write / 明示 deref / ポインタ算術 */
  assert(direct()[0][1][2] == 7);
  assert(direct()[1][2][3] == 24);
  direct()[1][1][1] = 77;
  assert(m[1][1][1] == 77);
  m[1][1][1] = 18;
  assert((*direct())[2][1] == 10);
  assert((*(direct() + 1))[0][0] == 13);
  assert(direct_from(1)[0][2][0] == 21);

  /* typedef local/global/struct member */
  assert(gi()[1][1][1] == 18);
  assert(gif(1)[0][0][2] == 15);
  assert(ggi()[1][2][0] == 21);
  assert(ops.gi()[0][2][3] == 12);
  assert(ops.gif(1)[0][0][2] == 15);

  /* 直書き global/struct member */
  assert(direct_global()[0][1][3] == 8);
  assert(direct_global_from(1)[0][1][1] == 18);
  assert(dops.gi()[1][0][2] == 15);
  assert(dops.gif(1)[0][1][1] == 18);

  /* fp 要素 */
  assert(ddirect()[1][0][2] == 9.5);
  assert((*ddirect())[1][1] == 5.5);
  assert(gd()[1][1][2] == 12.5);
  assert(ggd()[0][1][0] == 4.5);
  assert(ops.gd()[1][0][1] == 8.5);
  return 0;
}
