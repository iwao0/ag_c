/* 配列へのポインタの VLA 形 `int (*p)[m]` (m はランタイム値) (C11 6.7.6.2)。
 * ローカルは「配列サイズには整数定数式が必要」(E3064) で拒否、仮引数 `int (*a)[n]` は
 * コンパイルは通るが行ストライドを実行時 n で計算せずサイレント miscompile していた。
 * 行ストライド (extent*elem) を実行時に計算する隠しスロット (vla_row_stride_frame_off) を
 * ローカル/仮引数とも確保し、subscript・ポインタ算術 (+/-)・inc/dec が実行時ストライドを
 * 参照するようにして直す (既存 2D-VLA 機構を再利用)。 */
#include <assert.h>

/* 仮引数 int (*a)[n] */
static int psum(int n, int (*a)[n]) {
  int s = 0;
  for (int i = 0; i < 2; i++)
    for (int j = 0; j < n; j++) s += a[i][j];
  return s;
}
static void pset(int n, int (*a)[n]) { a[1][2] = 99; }
static double dread(int n, double (*a)[n]) { return a[1][2]; }

int main(void) {
  int m = 3;
  int a[2][3] = {{1, 2, 3}, {4, 5, 6}};

  /* ローカル subscript read / write */
  int (*p)[m] = a;
  assert(p[1][2] == 6 && p[0][1] == 2);
  p[1][0] = 40;
  assert(a[1][0] == 40);
  p[1][0] = 4;

  /* 初期化子なし + 代入、コピー */
  int (*q)[m];
  q = a;
  assert(q[1][1] == 5);
  int (*r)[m] = p;
  assert(r[0][2] == 3);

  /* ポインタ算術 (+/-) と deref */
  assert((*(p + 1))[2] == 6);
  int (*p2)[m] = p + 1;
  assert(p2[0][0] == 4 && (*(p2 - 1))[0] == 1);

  /* inc/dec */
  int (*pw)[m] = a;
  pw++;
  assert((*pw)[0] == 4);
  --pw;
  assert((*pw)[0] == 1);

  /* 複数 VLA ポインタが別ストライドで共存 */
  int k = 2;
  int b[2][2] = {{10, 20}, {30, 40}};
  int (*pb)[k] = b;
  assert(p[1][1] == 5 && pb[1][1] == 40);

  /* fp 要素 (double / float) */
  double da[2][3] = {{1.5, 2.5, 3.5}, {4.5, 5.5, 6.5}};
  double (*dp)[m] = da;
  assert(dp[1][2] == 6.5);
  dp[0][1] = 9.5;
  assert(da[0][1] == 9.5);
  float fa[2][2] = {{1.5f, 2.5f}, {3.5f, 4.5f}};
  float (*fp)[k] = fa;
  assert(fp[1][0] == 3.5f);

  /* struct 要素 */
  struct S { int x, y; };
  struct S sa[2][2] = {{{1, 2}, {3, 4}}, {{5, 6}, {7, 8}}};
  struct S (*sp)[k] = sa;
  assert(sp[1][1].x == 7 && sp[0][1].y == 4);

  /* sizeof は target のポインタサイズ */
  assert(sizeof(p) == sizeof(void*));

  /* 仮引数 int (*a)[n] read / write / fp */
  assert(psum(3, a) == 1 + 2 + 3 + 4 + 5 + 6 - 0);
  int z[2][3] = {{0, 0, 0}, {0, 0, 0}};
  pset(3, z);
  assert(z[1][2] == 99);
  assert(dread(3, da) == 6.5);

  return 0;
}
