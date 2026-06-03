// typedef 配列にユーザー追加の [N] suffix を結合する。
// `typedef int M[3][4]; M arr[2];` で arr は int[2][3][4] と等価。
// 修正前: ユーザー suffix [2] だけが反映され、arr が 8B しか確保されず SEGV。
// 修正: parse_decl_array_suffixes_constexpr_required で typedef dims を
// trailing_dims に append し、合計 dims [2][3][4] で outer/mid/extra
// strides を計算する。
// arr[1][2][3] = 1*100 + 2*10 + 3 = 123
// 期待: exit=123
typedef int M3[3][4];
int main(void) {
    M3 arr[2];
    int i, j, k;
    for (i = 0; i < 2; i++)
        for (j = 0; j < 3; j++)
            for (k = 0; k < 4; k++)
                arr[i][j][k] = i * 100 + j * 10 + k;
    return arr[1][2][3];
}
