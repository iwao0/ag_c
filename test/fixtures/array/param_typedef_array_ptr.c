// typedef した配列型を仮引数で受ける形 `row_t *a` (row_t = int[N])
// 修正前: typedef 解決で配列形状の情報が伝搬されず、`*a` が
//        単純な int* として扱われ a[i][j] が誤った値を返していた (exit=240)
// 対応: typedef 表に `is_array` を追加し、param_decl_spec_t に伝搬。
//      `param_ptr_levels==1 && typedef_is_array` のときに
//      outer_stride = sizeof_size を設定する。
// 期待: exit=6 (b[1][2])
typedef int row_t[3];
int get(row_t *a, int i, int j) {
    return a[i][j];
}
int main(void) {
    int b[2][3] = {{1,2,3}, {4,5,6}};
    return get(b, 1, 2);
}
