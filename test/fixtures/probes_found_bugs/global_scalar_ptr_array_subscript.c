// グローバル `char *names[3]` の `names[i][j]` 形式 2 段 subscript
// 修正前: register_toplevel_global_decl が `char *names[N]` を
// is_ptr=1 + is_array=1 として登録するとき deref_size=8 (ポインタサイズ)
// のみ保存し、pointee の素のサイズ (char=1) は捨てていた。
// try_build_global_var_node の配列 ND_ADDR が deref_size=8 のまま 2 段目
// subscript に渡るため、`names[2][3]` で offset が 3*8=24 にスケーリング
// されて out-of-bounds 読み出し → garbage 値が混入。
//
// 修正:
// - global_var_t に short pointee_elem_size を追加。is_ptr && is_array の
//   とき g_toplevel_decl_elem_size (= char なら 1) を保存。
// - canonical array element typeにscalar pointer shapeを保持する。
// - build_subscript_derefがそのelement typeからpointer result typeを構築する。
//   これで struct メンバ char* と同じsemantics
//   になり、subscript_base_address_of が ND_DEREF をそのまま返してポインタ値
//   load を引き起こす。
#include <assert.h>
char *names[3] = {"abc", "de", "fghi"};
int main(void) {
  assert(names[0][0] == 'a'); assert(names[1][1] == 'e'); assert(names[2][3] == 'i'); return 0; // 'a'+'e'+'i' = 97+101+105 = 303 → 47
}
// 期待: 47
