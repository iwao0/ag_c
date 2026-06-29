// 続き70: struct メンバ `int (*p)[N]` (配列へのポインタ) を struct メンバとして使う形 (修正)。
//
// 修正前: parser が struct メンバ宣言子 `int (*p)[N]` を `int *p[N]` (配列要素はポインタ)
// と区別せず両方とも paren_array_mul=1, is_ptr=1, trailing-array=N として解析していた。
// 違いは「`*` が parens の内側にあるか外側にあるか」だが、struct_layout 経路はその区別を
// 保持していなかった。結果:
//   - sizeof(struct H) が 8 ではなく 24 になる (3 ポインタぶんの誤レイアウト)。
//   - `(*g_h.p)[i]` が SIGSEGV (`*g_h.p` を int 1 個 load → スカラ + i を address として deref)。
//
// 修正: member_decl_head_t に ptr_in_paren を追加し、parse_member_decl_name_recursive で
// `(` 内で `*` が消費された事実を持ち回す。ptr_in_paren=1 かつ paren_array_mul=1 のとき、
// trailing `[N]` は pointee の配列次元として扱い、メンバ自身は単一ポインタ (8B) に。pointee の
// 全バイト数 (N*elem) は tag_member_info.outer_stride に保存する。build_member_deref_node に
// pointer-to-array 用の分岐 (is_tag_pointer + outer_stride>0) を追加し、ローカル `int (*p)[N]` の
// lvar (deref_size=outer_stride、is_pointer=1) と同じレイアウトで ND_DEREF を組む。
// build_unary_deref_node に「probe が ND_DEREF で outer_stride/elem 情報を carry している」分岐を
// 追加し、`*s.p` 構築時に新 deref の deref_size を elem に再設定 → subscript_base_address_of が
// lhs (= s.p) を返し、`(*s.p)[i]` がポインタ値 load + elem ストライド add で解決される。
//
// `int (*p[M])[N]` (parens 内に `[M]` あり、array-of-pointer-to-array) は別 fixture
// struct_array_of_ptr_to_array_member.c で対応済み。
#include <assert.h>
#include <stddef.h>

struct H { int (*p)[3]; };
int g_arr[3] = {10, 20, 30};
int g_buf[3];

struct H g_h = { &g_arr };

int main(void) {
    /* グローバル struct から read */
    assert(sizeof(struct H) == sizeof(int*));
    assert((*g_h.p)[0] == 10);
    assert((*g_h.p)[1] == 20);
    assert((*g_h.p)[2] == 30);

    /* ローカル struct ＋ アドレス代入 + read */
    struct H h_loc;
    h_loc.p = &g_arr;
    assert((*h_loc.p)[0] == 10);
    assert((*h_loc.p)[2] == 30);

    /* struct ポインタ経由 (`->` アクセス) */
    struct H *hp = &h_loc;
    assert((*hp->p)[1] == 20);
    assert((*hp->p)[2] == 30);

    /* write 経路: (*h.p)[i] = v */
    h_loc.p = &g_buf;
    (*h_loc.p)[0] = 100;
    (*h_loc.p)[1] = 200;
    (*h_loc.p)[2] = 300;
    assert(g_buf[0] == 100);
    assert(g_buf[1] == 200);
    assert(g_buf[2] == 300);

    /* `int *p[N]` (配列要素がポインタ) は別形で従来動作: 区別が保たれていることを確認 */
    struct A { int *q[3]; };
    assert(sizeof(struct A) == 3 * sizeof(int*));

    return 0;
}
