// typedef した 2 次元配列型を仮引数 ptr で受ける `typedef int M[3][4]; M *p`
// 修正前: 1 次元 typedef 配列は対応していたが、多次元では mid_stride が
//        設定されず a[i][j][k] の j ステップが誤った値だった (SEGV や 異常値)
// 対応: decl_array_suffix_t / toplevel_array_suffix_t に first_dim を追加し、
//      typedef 表に保存。param_decl_spec_t 経由で param_decl まで伝搬し、
//      mid_stride = sizeof_size / first_dim = (3*4*4)/3 = 16 を設定。
// 期待: exit=23 (b[1][2][3] = 1*12 + 2*4 + 3 = 23)
#include <assert.h>
typedef int M[3][4];
int get(M *a, int i, int j, int k) {
    return a[i][j][k];
}
int main(void) {
    int b[2][3][4];
    int v = 0, i, j, k;
    for (i = 0; i < 2; i++)
        for (j = 0; j < 3; j++)
            for (k = 0; k < 4; k++)
                b[i][j][k] = v++;
    /* b[i][j][k] = i*12 + j*4 + k */
    assert(get(b, 0, 0, 0) == 0);
    assert(get(b, 0, 0, 1) == 1);
    assert(get(b, 0, 1, 0) == 4);
    assert(get(b, 0, 2, 3) == 11);
    assert(get(b, 1, 0, 0) == 12);
    assert(get(b, 1, 1, 0) == 16);
    assert(get(b, 1, 2, 0) == 20);
    assert(get(b, 1, 2, 3) == 23);
    return 0;
}
