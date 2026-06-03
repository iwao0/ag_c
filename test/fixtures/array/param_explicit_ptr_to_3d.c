// 明示形「3D 配列へのポインタ」仮引数: int (*a)[N][M]
// 修正前: SEGV
// 対応: 上記 2D ケースと同じく、`(*a)` の inner pointer を見て続く
//      `[N][M]` を pointee dim として捕捉し、outer_stride=N*M*elem,
//      mid_stride=M*elem を設定する。
// 期待: exit=9 (b[1][0][2])
int get(int (*a)[2][3], int i, int j, int k) {
    return a[i][j][k];
}
int main(void) {
    int b[2][2][3] = {{{1,2,3},{4,5,6}}, {{7,8,9},{10,11,12}}};
    return get(b, 1, 0, 2);
}
