// compound literal で関数ポインタ配列を初期化 `(int (*[N])(args)){...}`
// 修正前: parse_cast_type は parse_array_of_funcptr_abstract_decl(&t, NULL) で
// 配列サイズを破棄しており、cast_array_count=0 のまま cast_is_ptr=1 で
// 初期化子経路に渡されていた。is_arr=0 → スカラ初期化子経路に流入し
// `{add, mul}` (2 要素) で E3025 ([decl] スカラ初期化子の波括弧内は1要素のみ).
//
// 修正:
// - parse_cast_type で parse_array_of_funcptr_abstract_decl に out_array_mul を
//   渡し、配列サイズを cast_array_count にセット。
// - parse_compound_literal_from_type で「関数ポインタ配列 compound literal」
//   (cast_is_ptr=1 && cast_array_count>0) を識別し、is_arr=1, base_elem=8,
//   var_size=N*8 で lvar を配列実体として登録。is_tag_pointer=0,
//   base_deref_size=8 はローカル `int (*ops[N])(args)` 経路 (decl.c:2259) と同じ。
// - psx_decl_parse_initializer_for_var は var->is_array=1 で自動的に
//   parse_array_initializer に流れる (decl.c:1806、既存経路)。
// - parse_array_initializer に拡張: `int arr[N] = (T[N]){...}` 形式
//   (=右辺が ND_COMMA(init, ND_ADDR(lvar))) を検出し、compound literal の lvar
//   から要素ごとに arr へ copy する init_chain を生成。これで Clang/GCC 拡張
//   と同じ「配列の compound literal initialization」が動く。
int add(int x, int y) { return x + y; }
int mul(int x, int y) { return x * y; }
int main(void) {
  int (*ops[2])(int, int) = (int (*[2])(int, int)){add, mul};
  return ops[0](3, 4) + ops[1](3, 4); // 7 + 12 = 19
}
// 期待: 19
