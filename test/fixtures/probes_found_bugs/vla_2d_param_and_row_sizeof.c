// 2 つの 2D VLA バグ:
//  A5: 2D VLA 仮引数の内側次元が「関数の第1パラメータ」(フレームオフセット 0) のとき、
//      emit_vla_row_stride_for_params が src_offset==0 を「未設定」と誤判定し行ストライド
//      計算を飛ばして未初期化スロットを読み、添字で SIGSEGV していた。frame_off 判定に変更。
//  A4: 2D VLA の行の sizeof `sizeof(a[0])` が要素サイズ (4) を返していた。行の実行時サイズ
//      (内側次元 * elem) は行ストライドスロットに既にあるので、sizeof でそれをロードして返す。
#include <assert.h>

static int rstride(int n, int a[n][n]) { return a[1][0] + a[2][1]; }
static int firstparam_dim(int m, int x, int a[x][m]) { return a[1][2]; }

int main(void) {
    /* A5: 内側次元が第1パラメータの 2D VLA 仮引数 */
    int a[3][3] = { {1,2,3}, {4,5,6}, {7,8,9} };
    assert(rstride(3, a) == 4 + 8);          /* a[1][0]=4, a[2][1]=8 */
    int b[2][4] = { {1,2,3,4}, {5,6,7,8} };
    assert(firstparam_dim(4, 2, b) == 7);    /* a[1][2]=7, inner dim m は第1param */

    /* A4: 2D VLA の行の sizeof は実行時の行サイズ */
    int n = 2, m = 4;
    int v[n][m];
    assert(sizeof(v[0]) == (unsigned long)m * sizeof(int));   /* 16 */
    int idx = 1;
    assert(sizeof(v[idx]) == 16);            /* 行ストライドは添字非依存 */
    assert(sizeof(v) == (unsigned long)n * m * sizeof(int));  /* 全体 32 (不変) */
    assert(sizeof(v[1][2]) == sizeof(int));  /* 要素は elem (不変) */

    /* 非 VLA / 1D VLA は不変 */
    int c[3][4];
    assert(sizeof(c[0]) == 16);
    int w[n];
    assert(sizeof(w[0]) == sizeof(int));
    return 0;
}
