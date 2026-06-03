// 非 variadic な double 戻り値関数を呼んで (int) キャスト
// 修正前: caller が引数を x0 (整数レジスタ) に置き、callee も x0 を
//        読んで scvtf で int → double 変換していた。結果として
//        7.0 のビットパターンが整数として渡され誤った値に。
// 対応:
//   - param_decl_spec_t に fp_kind を追加し、param lvar と args ノード
//     にも fp_kind を伝搬
//   - emit_param_save_to_frame で fp_kind を見て str d_reg/s_reg
//   - emit_funcall_pop_args_to_regs で fp_kind を見て ldr d_reg/s_reg
//   - 整数レジスタと FP レジスタを独立に index する (ARM64 ABI)
// 期待: exit=7
double id(double x) { return x; }
int main(void) {
    return (int)id(7.0);
}
