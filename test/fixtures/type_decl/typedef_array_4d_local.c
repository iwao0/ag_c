// 4 次元 typedef 配列をローカル変数として使う。
// `typedef int M4[2][3][4][5]; M4 a;` で a は int[2][3][4][5] と等価。
// outer_stride=240 (3*4*5*4B), mid_stride=80 (4*5*4B),
// extra_strides=[20 (5*4B), 4 (elem)], extra_strides_count=2。
// a[1][2][3][4] = 1*1000 + 2*100 + 3*10 + 4 = 1234
// exit code は 8bit なので 1234 % 256 = 210
// 期待: exit=210
typedef int M4[2][3][4][5];
int main(void) {
    M4 a;
    int i, j, k, l;
    for (i = 0; i < 2; i++)
        for (j = 0; j < 3; j++)
            for (k = 0; k < 4; k++)
                for (l = 0; l < 5; l++)
                    a[i][j][k][l] = i * 1000 + j * 100 + k * 10 + l;
    return a[1][2][3][4];
}
