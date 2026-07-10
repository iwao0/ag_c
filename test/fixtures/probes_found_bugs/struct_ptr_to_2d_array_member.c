// 続き72: struct メンバ `int (*p)[M][N]` (2 次元 pointee の pointer-to-array) (修正)。
// 続き70 の単一次元 pointee 対応の延長。
//
// 修正前: 続き70 で sizeof(struct H)=8 と単一ポインタ判定までは正しく動いていたが、
// pointee の次元情報 (M, N) を outer_stride に「全バイト数」しか保存していなかったため、
// `(*g_h.p)[i][j]` の 1 段目 subscript で elem 段の stride になり、結果 SIGSEGV/誤値。
//
// 修正:
// - struct_layout.c で pointee_arr_dim_count / pointee_arr_first_dim を保存し、2D pointee
//   のとき mid_stride (= N*elem) も tag_member_info descriptor に保存する。
// - build_member_deref_node の pointer-to-array 分岐で mem_info->mid_stride>0 のとき
//   deref->inner_deref_size に mid_stride を、deref->next_deref_size に elem を置き、
//   ローカル `int (*p)[M][N]` の lvar 表現と整合させる。
// - build_unary_deref_node の続き70 分岐 (ND_DEREF operand) で probe->next_deref_size>0 を
//   見て、結果 deref の inner_deref_size に carry。1 段スライドして次段 subscript が
//   inner_deref_size 段の stride で添字できるようにする。
//
// `int (*p)[N]` (1D) と `int *p[N]` (配列要素ポインタ) の挙動は不変。
#include <assert.h>
#include <stddef.h>

struct H { int (*p)[2][3]; };
int g_m[2][3] = {{1,2,3},{4,5,6}};
int g_w[2][3];

struct H g_h = { &g_m };

int main(void) {
    /* sizeof は単一ポインタ */
    assert(sizeof(struct H) == sizeof(int*));

    /* グローバル: 2D pointee access */
    assert((*g_h.p)[0][0] == 1);
    assert((*g_h.p)[0][2] == 3);
    assert((*g_h.p)[1][0] == 4);
    assert((*g_h.p)[1][2] == 6);

    /* ローカル struct + addr 代入 */
    struct H h;
    h.p = &g_m;
    assert((*h.p)[0][1] == 2);
    assert((*h.p)[1][1] == 5);

    /* `->` 経由 */
    struct H *hp = &h;
    assert((*hp->p)[0][2] == 3);
    assert((*hp->p)[1][2] == 6);

    /* write 経路 */
    h.p = &g_w;
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 3; j++)
            (*h.p)[i][j] = (i + 1) * 10 + j;
    assert(g_w[0][0] == 10 && g_w[0][2] == 12);
    assert(g_w[1][0] == 20 && g_w[1][2] == 22);

    return 0;
}
