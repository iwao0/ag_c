/* ポインタを返す関数の戻り値を subscript / ポインタ算術する `g()[i]` / `*(g()+i)` (C11 6.5.2.2)。
 * parser がポインタ戻り値の pointee 型を覚えておらず、ND_FUNCALL の deref_size が 0 だったため
 * subscript/算術がスケールせず (1 バイト加算) miscompile、`double *g()` は戻り値を d0 から
 * 誤読して SIGSEGV、`unsigned char *g()` は符号拡張していた。
 * 修正: (1) semantic ctx の戻り値型 (tag/token_kind) から pointee サイズ・fp 種別・符号を導出し
 * ND_FUNCALL の各アクセサ (deref_size/type_size/pointee_fp_kind) に反映、(2) ポインタ返しの
 * fp_kind を funcall ノードに立てない (戻り値は x0 のポインタ)、(3) pointee の unsigned を
 * subscript の zero-extend に伝播。`g()[i]` は `getbuf()[2]` が type_size=4 偶然一致で動いていた。 */
#include <assert.h>

static int    ib[5] = {10, 20, 30, 40, 50};
static long   lb[3] = {100, 200, 300};
static char   cb[4] = "abc";
static unsigned char ucb[3] = {200, 201, 202};
static signed char   scb[3] = {-1, -2, -3};
static unsigned short usb[3] = {60000, 60001, 60002};
static double db[4] = {1.5, 2.5, 3.5, 4.5};
static float  fb[4] = {1.5f, 2.5f, 3.5f, 4.5f};
struct P { int x, y; };
static struct P pb[3] = {{1, 2}, {3, 4}, {5, 6}};

static int*    gi(void) { return ib; }
static long*   gl(void) { return lb; }
static char*   gc(void) { return cb; }
static unsigned char*  guc(void) { return ucb; }
static signed char*    gsc(void) { return scb; }
static unsigned short* gus(void) { return usb; }
static double* gd(void) { return db; }
static float*  gf(void) { return fb; }
struct P* gp(void) { return pb; }  /* 非 static: `static struct T *f()` は別の parse バグ (別途) */
static void*   gv(void) { return ib; }
static int sq(int x) { return x * x; }

int main(void) {
  /* int*: subscript / ポインタ算術 (+/-) / 変数添字 */
  assert(gi()[2] == 30);
  assert(*(gi() + 3) == 40);
  assert(*(gi() + 4 - 1) == 40);
  int k = 1;
  assert(gi()[k] == 20);
  assert(gi() == ib);            /* ポインタ比較 (スケールの影響を受けない) */

  /* long* / char* */
  assert(gl()[2] == 300);
  assert(gc()[1] == 'b');

  /* unsigned 系: zero-extend */
  assert(guc()[1] == 201);
  assert(gus()[2] == 60002);
  /* signed char: sign-extend (非回帰) */
  assert(gsc()[1] == -2);

  /* double* / float*: fp load (旧 SIGSEGV) と書き込み */
  assert(gd()[2] == 3.5);
  assert(*(gd() + 3) == 4.5);
  assert(gf()[1] == 2.5f);
  gd()[0] = 9.5;
  assert(db[0] == 9.5);

  /* struct*: subscript .member / arrow */
  assert(gp()[2].x == 5 && gp()[1].y == 4);
  assert(gp()->x == 1);

  /* void* cast / ネスト呼び出し */
  int *vp = (int *)gv();
  assert(vp[1] == 20);
  assert(sq(gi()[1]) == 400);
  return 0;
}
