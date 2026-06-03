// 3 次元配列のネスト初期化と添字アクセス
// 修正前: 添字 `a[i][j][k]` の codegen が壊れていた:
//        1) `outer_stride = inner_dim * elem` (2D 用) のままで、3D の最外側
//           ストライドが N3 1 つ分しか進まなかった
//        2) ネスト初期化 `{{{...},{...}}, ...}` を parse_array_initializer が
//           扱えなかった (2 段までしか対応)
// 対応:
// - lvar_t に mid_stride、node_mem_t に next_deref_size を追加し、
//   サブスクリプトチェーンで「1段先」「2段先」のストライドを伝搬
// - parse_array_initializer に sub_row_len による中段 `{...}` の再帰処理を追加
// 期待: exit=12
int main(void) {
    int a[2][2][3] = {{{1,2,3},{4,5,6}}, {{7,8,9},{10,11,12}}};
    return a[1][1][2];
}
