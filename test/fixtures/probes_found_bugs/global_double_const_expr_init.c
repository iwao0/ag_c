// グローバル double を定数式 (`1.5 + 2.5`) で初期化
// 修正前: apply_toplevel_object_initializer は init_expr が ND_NUM のときのみ
// fval を取り、定数式 (`1.5 + 2.5` = ND_ADD(1.5, 2.5)) は const_ok 経路で
// 整数 0 として保存されていた → グローバル double が 0.0 で出力されていた。
//
// 修正: psx_eval_const_fp を parser.c に追加。ND_NUM / ND_ADD/SUB/MUL/DIV を
// 再帰評価して double を返す。グローバル fp_kind != NONE のとき、ND_NUM
// 経路の後に「fp 定数式」経路を追加して fp_folded を fval に保存。
double v = 1.5 + 2.5;  // 4.0
int main(void) {
  return (int)(v * 10); // 40
}
// 期待: 40
