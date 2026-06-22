// 混在 const/VLA dim サポート: 第 1 dim が const でも後の dim が VLA なら配列全体は
// VLA (C11 6.7.6.2)。`int t[2][n][4]` 等が E3064 で弾かれていた回帰。
//
// 修正: trailing `[...]` を token peek で走査し、TK_IDENT (enum 定数以外) があれば
// VLA 経路へ redirect。const 第 1 dim は ND_NUM ノードに包んで size_node として
// register_vla_lvar_and_append_alloc に渡す。
#include <assert.h>
#include <stddef.h>

enum { SIZE = 5 };

int main(void) {
    int n = 3;

    /* (a) 2D: const-first, VLA-inner */
    {
        int a[2][n];
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < n; j++) a[i][j] = i * 10 + j;
        int s = 0;
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < n; j++) s += a[i][j];
        /* 0+1+2 + 10+11+12 = 36 */
        assert(s == 36);
        assert(sizeof(a) == (size_t)(2 * n * 4));
        assert(sizeof(a[0]) == (size_t)(n * 4));
    }

    /* (b) 3D: [C][n][C] */
    {
        int b[2][n][4];
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < n; j++)
                for (int k = 0; k < 4; k++) b[i][j][k] = i + j + k;
        int s = 0, exp = 0;
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < n; j++)
                for (int k = 0; k < 4; k++) { s += b[i][j][k]; exp += i + j + k; }
        assert(s == exp);
        assert(sizeof(b) == (size_t)(2 * n * 4 * 4));
        assert(sizeof(b[0]) == (size_t)(n * 4 * 4));
        assert(sizeof(b[0][0]) == (size_t)(4 * 4));
    }

    /* (c) 3D: [C][C][n] (2 つの runtime stride) */
    {
        int c[2][3][n];
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < 3; j++)
                for (int k = 0; k < n; k++) c[i][j][k] = i + j * 10 + k * 100;
        int s = 0, exp = 0;
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < 3; j++)
                for (int k = 0; k < n; k++) { s += c[i][j][k]; exp += i + j*10 + k*100; }
        assert(s == exp);
        assert(sizeof(c) == (size_t)(2 * 3 * n * 4));
        assert(sizeof(c[0]) == (size_t)(3 * n * 4));
        assert(sizeof(c[0][0]) == (size_t)(n * 4));
    }

    /* (d) 3D: 全 VLA */
    {
        int d[n][n][n];
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++)
                for (int k = 0; k < n; k++) d[i][j][k] = (i+1)*(j+1)*(k+1);
        int s = 0, exp = 0;
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++)
                for (int k = 0; k < n; k++) { s += d[i][j][k]; exp += (i+1)*(j+1)*(k+1); }
        assert(s == exp);
    }

    /* (e) enum 定数を array dim に使う (false positive で VLA 経路に乗っても結果は正しい) */
    {
        int e[2][SIZE];
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < SIZE; j++) e[i][j] = i + j;
        int s = 0, exp = 0;
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < SIZE; j++) { s += e[i][j]; exp += i + j; }
        assert(s == exp);
    }

    return 0;
}
