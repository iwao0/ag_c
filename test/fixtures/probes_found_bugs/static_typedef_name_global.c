/* ファイルスコープの `static <typedef名> 変数` (`static Point p;`) (C11 6.7.1 / 6.2.2)。
 * parse_toplevel_decl_spec が storage class (static/extern) を tag キーワードの前でしか
 * skip せず、typedef 名の前では skip していなかったため、`static Point p` で `static` が残り
 * `Point` が型と認識されず E2006 (`;` 期待、実際 'p') で拒否されていた。非 static や
 * builtin (`static int`) や tag (`static struct S`) は動作。typedef 名の前にも修飾子先読み
 * skip を効かせて直す。const 修飾・ポインタ・配列・関数戻り型も同根なので併せて確認。 */
#include <assert.h>

typedef struct { int x, y; } Point;     /* 匿名 struct typedef */
typedef struct P { int a, b; } NP;      /* named struct typedef */
typedef int *IntPtr;

static Point sp = {3, 4};
static Point *spp = &sp;
static const Point csp = {7, 8};
static Point sarr[2] = {{1, 2}, {5, 6}};
static NP snp = {10, 20};
static IntPtr sip;                       /* typedef ポインタ */

static Point *get_sp(void) { return &sp; }

int main(void) {
  assert(sp.x == 3 && sp.y == 4);
  assert(spp->x == 3 && spp->y == 4);
  assert(csp.x == 7 && csp.y == 8);
  assert(sarr[0].x == 1 && sarr[1].y == 6);
  assert(snp.a == 10 && snp.b == 20);
  int v = 42;
  sip = &v;
  assert(*sip == 42);
  assert(get_sp()->x == 3);
  return 0;
}
