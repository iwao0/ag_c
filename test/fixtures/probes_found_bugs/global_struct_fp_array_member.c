/* グローバル struct の float/double 配列メンバを brace で初期化:
 *   struct R{double m[2][2];}; struct R r = { .m = {{1.5,2.5},{3.5,4.5}} };
 *
 * 以前は emit_global_struct_members_rec の配列メンバ経路 (タグ無し非ポインタ要素) が
 * cg_emit_int_directive(ts, ev) を直接呼び、mi.fp_kind と efv (init_fvalues 由来の
 * double 値) を見ずに整数 ev=0 を出力していた。結果 `.quad 0` だけが並び、double
 * 配列メンバ全体が 0 に化けていた。
 *
 * 修正: 配列メンバ経路でも emit_global_init_member_scalar を呼んで fp_kind/efv を見せる
 * (シンボル/関数ポインタ経路と統一)。シンボル無しでも fp_kind が立っていれば fp 値を、
 * そうでなければ整数を出力する。 */
#include <assert.h>

/* (1) 基本形: 2D double */
struct R { double m[2][2]; };
struct R r = { .m = { {1.5, 2.5}, {3.5, 4.5} } };

/* (2) 1D float (回帰確認) */
struct F { float v[3]; };
struct F f = { { 0.25f, 0.5f, 0.75f } };

/* (3) スカラ先頭 + double 配列 */
struct M { int n; double v[2]; };
struct M m = { 7, { 1.0, 2.0 } };

/* (4) double 配列 + 後続 int */
struct N { double v[2]; int n; };
struct N n = { { 10.5, 20.5 }, 33 };

int main(void) {
  assert(r.m[0][0] == 1.5 && r.m[0][1] == 2.5);
  assert(r.m[1][0] == 3.5 && r.m[1][1] == 4.5);

  assert(f.v[0] == 0.25f && f.v[1] == 0.5f && f.v[2] == 0.75f);

  assert(m.n == 7 && m.v[0] == 1.0 && m.v[1] == 2.0);

  assert(n.v[0] == 10.5 && n.v[1] == 20.5 && n.n == 33);

  return 0;
}
