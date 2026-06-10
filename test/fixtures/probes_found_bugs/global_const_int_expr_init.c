// 整数定数で初期化された const グローバルを別の const グローバル初期化子で参照
// 修正前: eval_const_expr_decl が ND_GVAR を「定数式ではない」として弾き、
// `const int C = A * B;` の初期化が constant fold できず apply_toplevel で
// 0 として保存されていた。
const int A = 5;
const int B = 7;
const int C = A * B;
int main(void) {
  return C; // 35
}
// 期待: 35
