// static local 整数配列 (`static int t[N] = {...};`) のグローバル lowering
// 修正前: ag_c は static local 配列を毎回フレーム上で再初期化する auto 配列
// として処理していたため、関数呼び出し間で値を保持できなかった
// (スカラ `static int n` だけ try_lower_static_local_scalar で lowering 済み、
//  配列は「既知の制約」とコメントに明記)。
//
// 修正:
// - decl.c に try_lower_static_local_array を新規追加。`[N]`/`[]` と
//   `= {...}` (brace 整数定数) のみ受け付け、文字列リテラル init / FP /
//   多次元 / struct は fallback (curtok 不変で peek)。
// - global_var_t (is_array=1, type_size, deref_size, init_values[]) を作り、
//   alias lvar (is_static_local=1, static_global_name=mangled, size=0,
//   is_array=0) を locals に挿入。alias の size=0 で frame 割当を抑制。
// - expr.c に build_static_local_array_addr_node を追加し、resolve_identifier
//   で lvar_is_static_local_array(var) のとき ND_ADDR(ND_GVAR) を返す。
//   global_vars 走査で gv->type_size を引いて sizeof 等で正しいサイズが
//   返るようにする。
// - parser.c の parse_global_brace_init_flat を psx_parse_global_brace_init_flat
//   として非 static 化 (internal/decl.h に extern 追加)。
#include <assert.h>
int touch(int i) {
  static int t[5] = {0, 0, 0, 0, 0};
  t[i]++;
  return t[i];
}
int main(void) {
  touch(0); touch(0); touch(0);
  touch(2); touch(2);
  assert(touch(0) == 4); assert(touch(2) == 3); return 0; // 4*100 + 3 = 403 → mod 256 = 147
}
// 期待: 147
