// 3D 以上の VLA 仮引数 `int t[n][m][k]` / `int t[n][m][k][l]` のサポート。
// register_vla_array_param が 2D までしか対応しておらず、3D 以上は内側 dim が
// silently 捨てられて miscompile (`sum_3d` が誤値) していた。
//
// 修正:
// (1) declarator の再帰配列型と runtime bound から内側 dim を全捕捉する。
// (2) lvar_t の動的 runtime descriptor に内側 dim 値の取得元
//     (const か frame offset か) を記録する。
// (3) register_vla_array_param: N-D VLA 仮引数で stride スロットを (N-1)*8 バイト確保し
//     vla_strides_remaining = n_inner - 1 を立てる。
// (4) emit_vla_row_stride_for_params: N-D VLA 仮引数の各 level の stride を関数 entry で
//     計算・store。後ろから掛けて各 level 1 回の MUL で済む構成。
//
// subscript chain は local VLA と同じ vla_row += 8 / vla_strides_remaining -= 1 の汎用機構を
// そのまま使う。
#include <assert.h>

int sum_3d(int n, int m, int k, int t[n][m][k]) {
    int s = 0;
    for (int i = 0; i < n; i++)
        for (int j = 0; j < m; j++)
            for (int p = 0; p < k; p++)
                s += t[i][j][p];
    return s;
}

int sum_4d(int n, int m, int k, int l, int t[n][m][k][l]) {
    int s = 0;
    for (int i = 0; i < n; i++)
        for (int j = 0; j < m; j++)
            for (int p = 0; p < k; p++)
                for (int q = 0; q < l; q++)
                    s += t[i][j][p][q];
    return s;
}

int sum_4d_mixed(int m, int k, int t[][m][3][k]) {
    int s = 0;
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < m; j++)
            for (int p = 0; p < 3; p++)
                for (int q = 0; q < k; q++)
                    s += t[i][j][p][q];
    return s;
}

int sum_3d_const(int t[][2][3]) {
    int s = 0;
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            for (int p = 0; p < 3; p++) s += t[i][j][p];
    return s;
}

int read_11d(int n, int t[][n][n][n][n][n][n][n][n][n][n]) {
    return t[0][0][0][0][0][0][0][0][0][0][0];
}

int main(void) {
    /* (a) 3D fully-VLA */
    int n = 2, m = 3, k = 4;
    int t3[n][m][k];
    int v = 1;
    for (int i = 0; i < n; i++)
        for (int j = 0; j < m; j++)
            for (int p = 0; p < k; p++) t3[i][j][p] = v++;
    assert(sum_3d(n, m, k, t3) == 300);  /* 1+..+24 = 300 */

    /* (b) 4D fully-VLA */
    int a=2, b=3, c=2, d=2;
    int t4[a][b][c][d];
    v = 1;
    for (int i=0;i<a;i++) for (int j=0;j<b;j++) for (int p=0;p<c;p++) for (int q=0;q<d;q++)
        t4[i][j][p][q] = v++;
    assert(sum_4d(a, b, c, d, t4) == 300);  /* 1..24 = 300 */

    /* (c) 4D mixed const/VLA */
    int mm = 2, kk = 3;
    int t4m[2][mm][3][kk];
    v = 1;
    for (int i=0;i<2;i++) for (int j=0;j<mm;j++) for (int p=0;p<3;p++) for (int q=0;q<kk;q++)
        t4m[i][j][p][q] = v++;
    assert(sum_4d_mixed(mm, kk, t4m) == 666);  /* 1..36 */

    /* (d) 3D all-const inner (regression: should still work) */
    int t3c[2][2][3];
    v = 1;
    for (int i=0;i<2;i++) for (int j=0;j<2;j++) for (int p=0;p<3;p++) t3c[i][j][p] = v++;
    assert(sum_3d_const(t3c) == 78);  /* 1..12 = 78 */

    /* (e) 旧 7-inner-dimension 上限を越える parameter VLA */
    int deep[1][1][1][1][1][1][1][1][1][1][1] = {7};
    assert(read_11d(1, deep) == 7);

    return 0;
}
