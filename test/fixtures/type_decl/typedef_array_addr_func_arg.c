// 3 次元 typedef 配列の `&a` を pointer-to-array 仮引数として関数に渡す。
// `void f(M *p)` で M=int[2][3][4] のとき、`f(&a)` のアドレス渡しが
// 修正前は E4002 で失敗していた。
// f 側では (*p)[1][2][3] で配列要素を読み戻し、ローカル変数経由の
// outer/mid stride 伝播 (`*p` の deref で typedef array dims を継承) が
// 正しく動くことを確認する。
// a[1][2][3] = 1*100 + 2*10 + 3 = 123
// 期待: exit=123
typedef int M[2][3][4];
int f(M *p) {
    return (*p)[1][2][3];
}
int main(void) {
    M a;
    int i, j, k;
    for (i = 0; i < 2; i++)
        for (j = 0; j < 3; j++)
            for (k = 0; k < 4; k++)
                a[i][j][k] = i * 100 + j * 10 + k;
    return f(&a);
}
