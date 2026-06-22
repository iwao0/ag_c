/* グローバル struct のネスト union メンバの fp 初期化:
 *   struct Inner { int a; union { int n; float f; } u; };
 *   struct Inner o = { 2, {.f = 2.5f} };  /* 旧: o.u.f = 0.0 *\/
 *
 * 以前は emit_global_struct_members_rec に TK_UNION 分岐が無く、union メンバが
 * 「mi.fp_kind=NONE のスカラ」として cg_emit_int_directive 経由で `.long 0` で出力されて
 * いた。global_var_t.union_init_ordinal はトップレベル union 専用で、ネスト union ごとの
 * active メンバを記録する仕組みが無いのが根本原因。
 *
 * 修正 (ヒューリスティック): emit_global_struct_members_rec に TK_UNION 分岐を追加。
 * `init_fvalues[*val_idx] != 0.0 && init_values[*val_idx] == 0` のとき、内側 union 内を
 * 巡回して最初に見つけた fp メンバを active と推定し、fp_kind / type_size を使って
 * emit_global_init_member_scalar に渡す。psx_gbrace_flat は ND_NUM 整数も `(double)val`
 * を fv に書く (fp 配列対応) ため、fv 単独で判別すると `.n = 99` も fp 扱いになるが、
 * iv==0 で絞れば「fp 値があり整数値が立っていない」= fp active と推定できる。
 * `.f = 0.0f` と `.n = 0` は両方とも fv=0/iv=0 で int 経路に流れるが、4B/8B のゼロビット
 * パターンは一致するので結果同じ。 */
#include <assert.h>

/* (1) 基本形: fp メンバ designator */
struct Inner1 { int a; union { int n; float f; } u; };
struct Inner1 o1 = { 2, {.f = 2.5f} };

/* (2) 同じ struct で int メンバ designator (回帰確認) */
struct Inner2 { int a; union { int n; float f; } u; };
struct Inner2 o2 = { 7, {.n = 99} };

/* (3) double メンバ designator */
struct D { int n; union { long l; double d; } u; };
struct D od = { 3, {.d = 3.14} };

/* (4) ネスト union を含む struct 配列 */
struct Inner3 { int a; union { int n; float f; } u; };
struct Inner3 oa[2] = {
  { 1, {.f = 1.5f} },
  { 2, {.n = 42} }
};

int main(void) {
  /* (1) */
  assert(o1.a == 2);
  assert(o1.u.f == 2.5f);

  /* (2) 回帰: 整数 designator は壊れない */
  assert(o2.a == 7);
  assert(o2.u.n == 99);

  /* (3) double メンバ */
  assert(od.n == 3);
  assert(od.u.d == 3.14);

  /* (4) 配列 */
  assert(oa[0].a == 1 && oa[0].u.f == 1.5f);
  assert(oa[1].a == 2 && oa[1].u.n == 42);

  return 0;
}
