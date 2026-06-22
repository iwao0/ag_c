/* ネスト union の active メンバを designator から明示通知する sentinel 機構:
 *   struct A { int a; union { float f; double d; } u; };
 *   struct A oa = { 1, {.f = 1.5f} };  /* active=f (4B float) *\/
 *   struct A ob = { 2, {.d = 3.14} };  /* active=d (8B double) *\/
 *
 * 以前は emit_global_struct_members_rec の TK_UNION 分岐がヒューリスティック
 * (init_fvalues != 0 && init_values == 0) で fp active を推定していた。これでは:
 *  - `.f = 0.0f` と `.n = 0` が両方 fv=0/iv=0 で判別不能 (ただし 4B ゼロビットパターンは
 *    一致するので結果に差は出ない)。
 *  - union 内に float と double 両方ある場合、ヒューリスティックは「最初の fp メンバ」を
 *    active として固定してしまい、type_size が誤る (float 1.5 を double 8B として、または
 *    double 3.14 を float 4B として出力)。
 *
 * 修正: parser psx_gbrace_flat の DOT 経路で union の fp メンバ designator を解決した時に
 * gbrace_ctx_t.pending_fp_kind / pending_fp_size を立てる。後段の scalar 書き込みで
 * init_value_symbols=NULL かつ init_value_symbol_lens に sentinel
 * (-2 = float, -3 = double/long double) を書く。emit TK_UNION 分岐が sentinel を最優先で
 * 読み取り fp_kind / type_size を正確に決定する。sentinel なしは旧ヒューリスティック fallback。
 * sentinel -1 (文字列リテラル要素) とは排他。 */
#include <assert.h>

/* (1) 基本: float designator */
struct A1 { int a; union { int n; float f; } u; };
struct A1 a1 = { 1, {.f = 1.5f} };

/* (2) 同 struct で int designator (回帰確認) */
struct A2 { int a; union { int n; float f; } u; };
struct A2 a2 = { 2, {.n = 99} };

/* (3) 同 struct で fp=0.0f designator (ヒューリスティック失敗ケース、sentinel で精密化) */
struct A3 { int a; union { int n; float f; } u; };
struct A3 a3 = { 3, {.f = 0.0f} };

/* (4) union 内に float と double 両方: float designator */
struct B1 { int a; union { float f; double d; } u; };
struct B1 b1 = { 10, {.f = 2.5f} };

/* (5) 同 struct で double designator (旧ヒューリスティックは「最初の fp」= float として
 * 出力してしまい型サイズが食い違っていた) */
struct B2 { int a; union { float f; double d; } u; };
struct B2 b2 = { 20, {.d = 3.14} };

/* (6) ネスト union を含む struct 配列 */
struct C { int a; union { int n; double d; } u; };
struct C cs[2] = {
  { 1, {.d = 1.25} },
  { 2, {.n = 42} },
};

int main(void) {
  assert(a1.a == 1 && a1.u.f == 1.5f);
  assert(a2.a == 2 && a2.u.n == 99);
  assert(a3.a == 3 && a3.u.f == 0.0f);

  assert(b1.a == 10 && b1.u.f == 2.5f);
  assert(b2.a == 20 && b2.u.d == 3.14);

  assert(cs[0].a == 1 && cs[0].u.d == 1.25);
  assert(cs[1].a == 2 && cs[1].u.n == 42);

  return 0;
}
