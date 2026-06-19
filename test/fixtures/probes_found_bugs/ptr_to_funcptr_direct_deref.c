// `int (**pp)(int)` 直接形式 (関数ポインタへのポインタ) の deref 呼び出し
// 修正前: consume_decl_name_recursive (decl.c:1701-1703) で paren-grouped
// 宣言子 `(*...)` の後ろに `[N]` が無くても parse_decl_array_suffixes_constexpr_required(1)
// が初期値 1 をそのまま返し、`paren_array_mul=1` を立てていた。これにより
// `int (**pp)(int)` が `(*p)[N]` 配列ポインタ専用ブランチ (decl.c:2246) を
// 誤発火し、`psx_decl_register_lvar_sized_align(.., 8, elem_size=4, 0)` で
// elem_size=4 + outer_stride=4 で登録 → build_lvar_or_vla_node が mem.deref_size=4
// を作り、`*pp` の ND_DEREF.type_size=4 が IR_TY_I32 と判定されて
// `ldrsw x19, [x20]` (4B signed load) を出していた。関数ポインタの上位 32bit が
// 落ちて不正アドレスを call → segfault。
//
// 修正: consume_decl_name_recursive で local_had_parens && curtok==TK_LBRACKET の
// ときだけ paren_array_mul を立てる。`[` 無しなら 0 のままで通常 else 分岐
// (decl.c:2445) に落ちて pointer_qual_levels と pointer_deref_size の正しい
// 計算経路に乗る。
//
// 経路差分:
// - typedef `fp_t *pp` は paren を含まないので影響なし
// - `int **pp` (p198) も paren なし、影響なし
// - `int (*p)[3]` は実際の `[3]` を消費するので paren_array_mul=3 で従来通り
// - `int (*ops[N])(int)` は inner_array_mul=N で別分岐 (2221)、独立
#include <assert.h>
int f(int x) { return x * 3; }
int main(void) {
  int (*p)(int) = f;
  int (**pp)(int) = &p;
  assert((*pp)(7) == 21); return 0; // 21
}
// 期待: 21
