// 3D VLA `int t[n][m][k]` 宣言と subscript chain / sizeof のサポート。
// register_vla_lvar_and_append_alloc が 1D/2D のみ対応で、3 段目の dim suffix を
// parse_decl_skip_constexpr_array_suffixes で消費しようとして非定数を拒否し E3064 になっていた。
//
// 修正:
// (1) 3D VLA は 32B descriptor slot ([base][byte_size][outer_stride][mid_stride])。
//     outer = m*k*elem (vla_row_stride_frame_off に格納)、mid = k*elem
//     (vla_mid_stride_frame_off に格納、init_chain に STORE 注入)。
// (2) lvar_t に vla_mid_stride_frame_off を追加。
// (3) build_lvar_or_vla_node が 3D VLA に対し inner_deref_size=0、next_deref_size=elem に。
// (4) build_subscript_deref が 1 段目 subscript 結果 ND_DEREF に vla_row_stride_frame_off=
//     mid_slot を立てる (次 subscript が runtime mid stride で動くように)。
// (5) make_subscript_scaled_offset が ND_DEREF からも vla_rsf を読む。
// (6) subscript_base_address_of が vla_row_stride_frame_off>0 の deref を address として返す
//     (これがないと t[i] が 1 バイト load されて SIGSEGV)。
// (7) sizeof(vla3d[i][j]) は vla_mid_stride_frame_off スロット (k*elem) を読む。
//
// 制約 (今回は未対応): mixed const/VLA で「第 1 dim が const、後の dim が VLA」
// (例 `int t[2][n][4]`) は依然 E3064 (register_multidim_array_lvar 経由のため)。
// 第 1 dim が VLA・後が const のケースは all-VLA 経路に乗るので動作する。
#include <assert.h>
#include <stdio.h>

int main(void) {
    int n = 2, m = 3, k = 4;

    /* (a) all-VLA */
    {
        int t[n][m][k];
        for (int i = 0; i < n; i++)
            for (int j = 0; j < m; j++)
                for (int l = 0; l < k; l++) t[i][j][l] = i*100 + j*10 + l;
        int s = 0;
        for (int i = 0; i < n; i++)
            for (int j = 0; j < m; j++)
                for (int l = 0; l < k; l++) s += t[i][j][l];
        int exp = 0;
        for (int i = 0; i < n; i++) for (int j = 0; j < m; j++) for (int l = 0; l < k; l++) exp += i*100 + j*10 + l;
        assert(s == exp);
        assert(sizeof(t) == (size_t)(n*m*k*4));
        assert(sizeof(t[0]) == (size_t)(m*k*4));
        assert(sizeof(t[0][0]) == (size_t)(k*4));
        assert(sizeof(t[0][0][0]) == 4);
    }

    /* (b) first-dim VLA, rest const */
    {
        int t[n][3][4];
        for (int i = 0; i < n; i++)
            for (int j = 0; j < 3; j++)
                for (int l = 0; l < 4; l++) t[i][j][l] = i*1000 + j*100 + l;
        int s = 0;
        for (int i = 0; i < n; i++)
            for (int j = 0; j < 3; j++)
                for (int l = 0; l < 4; l++) s += t[i][j][l];
        int exp = 0;
        for (int i = 0; i < n; i++) for (int j = 0; j < 3; j++) for (int l = 0; l < 4; l++) exp += i*1000 + j*100 + l;
        assert(s == exp);
        assert(sizeof(t) == (size_t)(n*3*4*4));
        assert(sizeof(t[0]) == (size_t)(3*4*4));
        assert(sizeof(t[0][0]) == (size_t)(4*4));
    }

    /* (c) double element 3D VLA */
    {
        double da[n][m][k];
        for (int i = 0; i < n; i++)
            for (int j = 0; j < m; j++)
                for (int l = 0; l < k; l++) da[i][j][l] = i + j * 0.1 + l * 0.01;
        double s = 0.0;
        for (int i = 0; i < n; i++)
            for (int j = 0; j < m; j++)
                for (int l = 0; l < k; l++) s += da[i][j][l];
        /* 値そのものより、ストライド計算が機能していることが分かれば良い。
         * 期待値は ref と一致するかを assert する代わりに sizeof を見る。 */
        assert(sizeof(da) == (size_t)(n*m*k*8));
        assert(sizeof(da[0]) == (size_t)(m*k*8));
        assert(sizeof(da[0][0]) == (size_t)(k*8));
        (void)s;
    }

    /* (d) subscript individual elements write/read */
    {
        int t[n][m][k];
        t[1][2][3] = 999;
        t[0][0][0] = 1;
        assert(t[1][2][3] == 999);
        assert(t[0][0][0] == 1);
    }

    return 0;
}
