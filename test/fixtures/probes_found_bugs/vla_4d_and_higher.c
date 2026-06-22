// 4D+ VLA `int t[n][m][k][l]...` のサポート。
// 続き30 で 3D VLA を 32B descriptor + vla_mid_stride_frame_off で実装したが、4D 以上は
// 「あと何個 runtime stride が残るか」を追跡できず E3064 で拒否されていた。
//
// 修正:
// (1) lvar_t.vla_mid_stride_frame_off を vla_strides_remaining に置換 (汎用化)。
//     vla_row_stride_frame_off = 次 subscript で消費する stride スロットの frame offset、
//     vla_strides_remaining = その後にまだ続く runtime stride スロット数。
// (2) node_mem_t にも vla_strides_remaining を追加し、subscript chain で carry。
// (3) register_vla_lvar_and_append_alloc を N-D 対応に書き換え:
//     slot サイズ = 16 + 8*(N-1) bytes、stride[k] (= dim[k+1]*...*dim[N-1]*elem) を
//     slot+16+8*k に store する。level 0 は VLA_ALLOC の rsf 経路で、level 1..N-2 は
//     init_chain への STORE 注入で初期化。最大 8 次元。
// (4) build_subscript_deref が parent.vla_row+8 / parent.remaining-1 を carry。同時に
//     parent.inner_deref_size (= elem) を carry し、4D 以降で「中間配列」と認識し続ける。
// (5) sizeof(vlaN[i][j]...[d]) は連続 [...] を D 段 peek して、slot+16+(D-1)*8 を読む。
//
// 制約: 8 次元超は parse_decl_skip_constexpr_array_suffixes で末尾消費し E3064 を残す。
#include <assert.h>
#include <stddef.h>

int main(void) {
    int n = 2, m = 3, k = 2, l = 4;

    /* (a) 4D 全 VLA */
    {
        int t[n][m][k][l];
        for (int i = 0; i < n; i++)
            for (int j = 0; j < m; j++)
                for (int p = 0; p < k; p++)
                    for (int q = 0; q < l; q++) t[i][j][p][q] = i*1000 + j*100 + p*10 + q;
        int s = 0, exp = 0;
        for (int i = 0; i < n; i++)
            for (int j = 0; j < m; j++)
                for (int p = 0; p < k; p++)
                    for (int q = 0; q < l; q++) {
                        s += t[i][j][p][q];
                        exp += i*1000 + j*100 + p*10 + q;
                    }
        assert(s == exp);
        assert(sizeof(t) == (size_t)(n*m*k*l*4));
        assert(sizeof(t[0]) == (size_t)(m*k*l*4));
        assert(sizeof(t[0][0]) == (size_t)(k*l*4));
        assert(sizeof(t[0][0][0]) == (size_t)(l*4));
        assert(sizeof(t[0][0][0][0]) == 4);
    }

    /* (b) 4D mixed const/VLA */
    {
        int t[2][m][3][l];
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < m; j++)
                for (int p = 0; p < 3; p++)
                    for (int q = 0; q < l; q++) t[i][j][p][q] = (i+1)*(j+1)*(p+1)*(q+1);
        int s = 0, exp = 0;
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < m; j++)
                for (int p = 0; p < 3; p++)
                    for (int q = 0; q < l; q++) {
                        s += t[i][j][p][q];
                        exp += (i+1)*(j+1)*(p+1)*(q+1);
                    }
        assert(s == exp);
    }

    /* (c) 5D 全 VLA */
    {
        int a = 2, b = 2, c = 2, d = 2, e = 2;
        int t[a][b][c][d][e];
        int v = 1;
        for (int i = 0; i < a; i++) for (int j = 0; j < b; j++) for (int p = 0; p < c; p++)
            for (int q = 0; q < d; q++) for (int r = 0; r < e; r++) t[i][j][p][q][r] = v++;
        int s = 0;
        for (int i = 0; i < a; i++) for (int j = 0; j < b; j++) for (int p = 0; p < c; p++)
            for (int q = 0; q < d; q++) for (int r = 0; r < e; r++) s += t[i][j][p][q][r];
        /* sum 1..32 = 32*33/2 = 528 */
        assert(s == 528);
        assert(sizeof(t) == (size_t)(2*2*2*2*2*4));
        assert(sizeof(t[0][0][0]) == (size_t)(2*2*4));
        assert(sizeof(t[0][0][0][0]) == (size_t)(2*4));
    }

    return 0;
}
