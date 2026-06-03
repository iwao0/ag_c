// 2 次元 typedef 配列をローカル変数として使う。
// `typedef int M[3][4]; M m;` で m は int[3][4] と等価。
// outer_stride=16, mid_stride=0 (2D), elem_size=4。
// 期待: m[2][3] = 23
typedef int M[3][4];
int main(void) {
    M m;
    int i, j;
    for (i = 0; i < 3; i++)
        for (j = 0; j < 4; j++)
            m[i][j] = i * 10 + j;
    return m[2][3];
}
