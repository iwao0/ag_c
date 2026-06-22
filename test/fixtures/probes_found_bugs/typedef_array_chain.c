// typedef chain で base typedef が自身配列の場合 (`typedef int Row[3]; typedef Row Matrix[2]`)
// の dims 合成: declarator dims と base typedef dims を結合して [declarator..., base...] とする。
// これがないと Matrix が int[2] として登録され sizeof(Matrix)=24 ではなく 8 になり、
// `Matrix m={{1,2,3},{4,5,6}}` も E3064 で弾かれていた。
//
// 関数内 typedef の通常配列 `typedef int Row[3]` も is_array=1 が立っていなかった
// (トップレベルと非対称) ので `Row r = {1,2,3}` が E3064 になっていた回帰も同時に修正。
#include <assert.h>

typedef int Row[3];
typedef Row Matrix[2];                  /* int[2][3] */

typedef int M23[2][3];
typedef M23 M4[4];                      /* int[4][2][3] */

typedef Row Cube[2][5];                 /* int[2][5][3] */

/* グローバル変数 */
M4 gm4 = {{{1,2,3},{4,5,6}}, {{7,8,9},{10,11,12}},
          {{13,14,15},{16,17,18}}, {{19,20,21},{22,23,24}}};
Matrix gmat = {{1,2,3},{4,5,6}};

int main(void) {
    /* sizeof checks (toplevel typedef chain) */
    assert(sizeof(Row) == 12);
    assert(sizeof(Matrix) == 24);
    assert(sizeof(M23) == 24);
    assert(sizeof(M4) == 96);
    assert(sizeof(Cube) == 120);

    /* Matrix init (nested brace) */
    Matrix m = {{10, 20, 30}, {40, 50, 60}};
    assert(m[0][0] == 10 && m[0][2] == 30);
    assert(m[1][1] == 50 && m[1][2] == 60);

    /* Matrix flat init */
    Matrix m2 = {1, 2, 3, 4, 5, 6};
    assert(m2[0][0] == 1 && m2[1][2] == 6);

    /* Multi-dim base */
    M4 m4 = {{{1,2,3},{4,5,6}}, {{7,8,9},{10,11,12}},
             {{13,14,15},{16,17,18}}, {{19,20,21},{22,23,24}}};
    assert(m4[0][0][0] == 1);
    assert(m4[3][1][2] == 24);

    /* Cube (declarator multi-dim + base array) */
    Cube cu;
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 5; j++)
            for (int k = 0; k < 3; k++) cu[i][j][k] = i*100 + j*10 + k;
    assert(cu[0][0][0] == 0);
    assert(cu[1][4][2] == 142);

    /* Global */
    assert(gm4[0][0][0] == 1 && gm4[3][1][2] == 24);
    assert(gmat[0][0] == 1 && gmat[1][2] == 6);

    /* Function-local typedef chain (parser path in stmt.c) */
    {
        typedef int LR[3];
        typedef LR LM[2];
        assert(sizeof(LR) == 12);
        assert(sizeof(LM) == 24);
        LR r = {10, 20, 30};         /* 基本: 通常の配列 typedef */
        LM lm = {{1,2,3},{4,5,6}};   /* chain */
        assert(r[0] == 10 && r[2] == 30);
        assert(lm[0][0] == 1 && lm[1][2] == 6);
    }

    return 0;
}
