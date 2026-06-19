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
// - node_mem_t に pointee_is_scalar_ptr フラグを追加。
//   try_build_global_var_node の配列ブランチで pointee_elem_size > 0 &&
//   tag_kind == TK_EOF (struct でないスカラ型ポインタ要素) のときに立てる。
// - build_subscript_deref で base の pointee_is_scalar_ptr が立っているとき、
//   結果 ND_DEREF に is_scalar_ptr_member=1 + deref_size=pointee_elem_size
//   を設定。これで struct メンバ char* (commit 6a663ed) と同じ semantics
//   になり、subscript_base_address_of が ND_DEREF をそのまま返してポインタ値
//   load を引き起こす。
#include <assert.h>
char *names[3] = {"abc", "de", "fghi"};
int main(void) {
  assert(names[0][0] == 'a'); assert(names[1][1] == 'e'); assert(names[2][3] == 'i'); return 0; // 'a'+'e'+'i' = 97+101+105 = 303 → 47
}
// 期待: 47
