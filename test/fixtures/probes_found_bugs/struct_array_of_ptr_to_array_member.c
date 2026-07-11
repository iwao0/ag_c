// 続き71: struct メンバ `int (*p[M])[N]` (array-of-pointer-to-array) を struct メンバ
// として使う形 (修正)。続き70 の `int (*p)[N]` (single pointer-to-array) の続き。
//
// 修正前: parser が `int (*p[M])[N]` を `int *p[M*N]` と区別せず、arr_size を M*N として
// 解析していた。結果 sizeof(struct H) が 8*M*N (M=2, N=3 で 48) と誤レイアウト、
// `(*g_h.p[i])[j]` も SIGSEGV / 誤値。
//
// 修正:
// - struct_layout.c の ptr_in_paren && paren_array_mul > 1 分岐を追加: arr_size = M、
//   ptr_array_pointee_bytes (= N * elem) を tag_member_info に保存。
// - canonical tag member typeにpointer-to-array shapeを保存し、匿名struct昇格でも保持。
// - build_subscript_derefがcanonical pointer-to-array typeから結果型を構築。これで
//   `(*s.p[i])[j]` の単項 `*` が build_unary_deref_node の続き70 で追加した
//   pointer-to-array 分岐 (operand=ND_DEREF && is_tag_pointer && inner_deref_size>0 &&
//   deref_size>inner_deref_size) に乗り、subscript_base_address_of が lhs を返す経路で
//   ポインタ load + 要素ストライド add で解決される。
//
// `int *p[M*N]` (区別) と `struct { int (*p)[N]; }` (続き70) の挙動はいずれも不変。
#include <assert.h>
#include <stddef.h>

struct H { int (*p[2])[3]; };
int g_a[3] = {10, 20, 30};
int g_b[3] = {40, 50, 60};
int g_buf1[3], g_buf2[3];

struct H g_h = { { &g_a, &g_b } };

int main(void) {
    /* sizeof: 2 ポインタ ぶん */
    assert(sizeof(struct H) == 2 * sizeof(int*));

    /* グローバル struct から read */
    assert((*g_h.p[0])[0] == 10);
    assert((*g_h.p[0])[2] == 30);
    assert((*g_h.p[1])[0] == 40);
    assert((*g_h.p[1])[2] == 60);

    /* ローカル struct + アドレス代入 */
    struct H h;
    h.p[0] = &g_a;
    h.p[1] = &g_b;
    assert((*h.p[0])[1] == 20);
    assert((*h.p[1])[1] == 50);

    /* struct ポインタ経由 (`->`) */
    struct H *hp = &h;
    assert((*hp->p[0])[2] == 30);
    assert((*hp->p[1])[0] == 40);

    /* write 経路 */
    h.p[0] = &g_buf1;
    h.p[1] = &g_buf2;
    for (int i = 0; i < 3; i++) {
        (*h.p[0])[i] = 100 + i;
        (*h.p[1])[i] = 200 + i;
    }
    assert(g_buf1[0] == 100 && g_buf1[1] == 101 && g_buf1[2] == 102);
    assert(g_buf2[0] == 200 && g_buf2[1] == 201 && g_buf2[2] == 202);

    /* 区別の保持: `int *q[M]` (配列要素がポインタ) は従来動作 */
    struct A { int *q[3]; };
    assert(sizeof(struct A) == 3 * sizeof(int*));

    return 0;
}
