// 2 次元配列 `int a[][3]` を仮引数に取る関数
// 修正前: 仮引数の lvar が「int *a」相当として登録され、内側 [3] が
//        無視されていた。a[i] のストライドが elem_size(=4) のまま進み、
//        意図しないオフセットを読んでいた。
// 対応: 最外側 `[]` を pointer 化扱いし、2 つ目以降の `[N]` を
//      pointee 全体のサイズに反映 (outer_stride = N*elem_size)。
// 期待: exit=6 (a[1][2])
int get(int a[][3], int i, int j) {
    return a[i][j];
}
int main(void) {
    int a[2][3] = {{1,2,3}, {4,5,6}};
    return get(a, 1, 2);
}
