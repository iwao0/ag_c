// 関数ポインタ配列を仮引数で受け取って呼び出す `int (*ops[])(int)`
// 修正前: parse_param_decl が `int (*ops[])(int)` を扱う専用パスを持たず、
// `param_is_ptr=1, param_is_array_declarator=1` のとき後続の `(int)` 関数
// suffix は `param_inner_first_dim=0` で識別できず、フォールバックで
// pointee_size = ds.elem_size = 4 (int) で登録 → ops[i] のステップが 4*i
// になって不正アドレス deref で segfault。
//
// ローカル `int (*ops[2])(int)` (decl.c の inner_array_mul + is_pointer 経路)
// は elem_size=8 (関数ポインタサイズ) で登録されていて正しく動く。
//
// 修正:
// - parse_param_declarator_name(_recursive) に out_has_func_suffix を追加。
//   関数 suffix `(...)` を skip するときフラグを立てる。
// - parse_param_decl で param_is_array_declarator && param_is_ptr &&
//   param_has_func_suffix && tag_kind==TK_EOF のとき、elem_size=8 で登録し
//   pointer_qual_levels=1 を立てて lvar_is_pointer を発火させる。
//   C11 6.7.6.3p7 で配列 → ポインタへ adjust された `int (**ops)(int)` 相当。
#include <assert.h>
int double_it(int x) { return x * 2; }
int add_one(int x) { return x + 1; }
int dispatch(int (*ops[])(int), int i, int val) {
  return ops[i](val);
}
int main(void) {
  int (*ops[2])(int) = {double_it, add_one};
  assert(dispatch(ops, 0, 21) == 42); assert(dispatch(ops, 1, 41) == 42); return 0; // 42+42 = 84
}
// 期待: 84
