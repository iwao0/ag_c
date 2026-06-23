// 続き73: グローバル plain 多次元配列の `[N]={[M]=...}` ネスト designator (修正)。
// 続き13 (struct メンバの多次元配列 designator) と類似だが、struct メンバではなく
// トップレベルの plain 多次元グローバル配列で発生した別経路のバグ。
//
// 修正前: `int g[3][2] = {[2]={[1]=99}};` で 99 が g[2][1] (flat slot 5) ではなく
// g[1][1] (flat slot 3) に書かれていた。psx_gbrace_flat の外側 `[2]=` の elem_slots
// 計算が gbrace_ctx_t.sub_dims を見るが、トップレベル global 配列用の ctx 初期化
// (psx_parse_global_brace_init_flat) は sub_dims を埋めておらず、`[2]=` が単一スカラ
// scale (slot 2) でジャンプ、続く `[1]=99` が slot 3 へ書く挙動。
//
// 修正: psx_parse_global_brace_init_flat で gv->outer_stride / mid_stride / extra_strides /
// deref_size の隣接ペアから sub_dims を算出して ctx に埋める。`int g[3][2]` なら
// sub_dims=[2]、`int g[3][2][4]` なら sub_dims=[2,4]、3D/4D も対応。続き13 と同じ
// elem_slots 計算経路 (sub_ndim>=1 で sub_dims の積) に乗る。
//
// struct メンバ多次元配列 (続き13、`struct {int x[3][3];} g = {.x={[0]={1,2,3}, [2]={[2]=9}}}`)
// は gbrace_ctx_from_member 経由でこれまで通り動作。
#include <assert.h>

/* 2D plain int 配列 */
int g2d[3][2] = {[2] = {[1] = 99}};

/* 3D plain int 配列 */
int g3d[3][2][4] = {[2] = {[1] = {[3] = 77}}};

/* 1 段 designator + 内側 positional */
int g2d_b[3][2] = {[1] = {10, 20}, [2] = {30, 40}};

/* designator なしの positional は不変 (回帰確認) */
int g_plain[2][3] = {{1, 2, 3}, {4, 5, 6}};

int main(void) {
    /* g2d: 99 は g2d[2][1] にあるべき */
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 2; j++) {
            if (i == 2 && j == 1) assert(g2d[i][j] == 99);
            else assert(g2d[i][j] == 0);
        }
    }

    /* g3d: 77 は g3d[2][1][3] にあるべき */
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 2; j++) {
            for (int k = 0; k < 4; k++) {
                if (i == 2 && j == 1 && k == 3) assert(g3d[i][j][k] == 77);
                else assert(g3d[i][j][k] == 0);
            }
        }
    }

    /* g2d_b: positional inner */
    assert(g2d_b[0][0] == 0 && g2d_b[0][1] == 0);
    assert(g2d_b[1][0] == 10 && g2d_b[1][1] == 20);
    assert(g2d_b[2][0] == 30 && g2d_b[2][1] == 40);

    /* g_plain: 完全 positional (designator なし) は変更なし */
    assert(g_plain[0][0] == 1 && g_plain[0][2] == 3);
    assert(g_plain[1][0] == 4 && g_plain[1][2] == 6);

    return 0;
}
