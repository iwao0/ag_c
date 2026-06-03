// 3 次元 typedef 配列を pointer-to-array 仮引数として受け取る。
// `typedef int M3[2][3][4]; int f(M3 *p)` で `(*p)[i][j][k]` がすべての段で
// 正しい stride を使う。
// 修正前: parser.c の M3* 仮引数 stride 設定が 3 次元の場合 mid/extra を
// 部分的にしか入れていなかった (3D の最深 stride が欠落)。
// 修正: 3+ 次元のとき outer=sizeof(M), mid=D1*..*elem, extra[k]=D(k+2)*..*elem
// を順に設定し、build_unary_deref_node で *p の deref node に 1 段スライドして
// 継承する。
// arr[1][2][3] = 1*100 + 2*10 + 3 = 123
// 期待: exit=123
typedef int M3[2][3][4];
int f(M3 *p) {
    return (*p)[1][2][3];
}
int main(void) {
    M3 arr;
    int i, j, k;
    for (i = 0; i < 2; i++)
        for (j = 0; j < 3; j++)
            for (k = 0; k < 4; k++)
                arr[i][j][k] = i * 100 + j * 10 + k;
    return f(&arr);
}
