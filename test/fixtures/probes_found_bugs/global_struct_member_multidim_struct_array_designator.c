// 続き74: グローバル struct メンバの 2D struct タグ配列で外側 `[N]=` designator (修正)。
//
// 修正前: `struct S { struct C rows[3][2]; } g = {.rows = {[2] = {{99},{100}}}};` で
// 99/100 が rows[2][0]/rows[2][1] でなく rows[1][0]/rows[1][1] に書かれていた。
//
// 原因:
// - struct_layout.c が「タグ無し多次元配列メンバ」のみ arr_dims を保存し、struct タグ配列
//   メンバ (member_tag_kind != TK_EOF) では arr_dims を捨てていた。
// - parser.c の gbrace_ctx_from_member も同様に `mi->tag_kind == TK_EOF` 限定で sub_dims を
//   埋めており、struct タグ配列の sub_dims が常に空。
// - psx_gbrace_flat の `[N]=` elem_slots 計算が tag_kind == TK_STRUCT 経路で struct 1 要素の
//   スロット数だけを返し、内側次元 (2) を考慮しなかった。
//
// 修正:
// - struct_layout.c: arr_dims 保存条件を `!member_is_ptr` に緩和。
// - parser.c gbrace_ctx_from_member: 条件を `mi->arr_ndim >= 2` に緩和し、struct タグ多次元
//   配列も sub_dims を carry。
// - psx_gbrace_flat の elem_slots 計算: struct 経路でも sub_dims の積を掛ける。
//
// 内側 designator `[2]={[1]=...}` や内側 brace 内の `.member=` designator は
// global_struct_member_multidim_nested_designator で対応済み。
#include <assert.h>

struct C { int val; };
struct S { struct C rows[3][2]; };
struct T { struct C grid[3][2][4]; };

/* 2D: 外側 designator + 内側 positional */
struct S g = {.rows = {[2] = {{99}, {100}}}};

/* 2D: 外側 designator が 2 つ */
struct S h = {.rows = {[0] = {{1}, {2}}, [2] = {{5}, {6}}}};

/* 3D: 外側 designator + 内側 positional */
struct T t = {.grid = {[2] = {{{1},{2},{3},{4}}, {{5},{6},{7},{8}}}}};

int main(void) {
    /* g: rows[2][0]=99, rows[2][1]=100、他は 0 */
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 2; j++) {
            if (i == 2 && j == 0) assert(g.rows[i][j].val == 99);
            else if (i == 2 && j == 1) assert(g.rows[i][j].val == 100);
            else assert(g.rows[i][j].val == 0);
        }
    }

    /* h: rows[0][0]=1, rows[0][1]=2, rows[1]=0, rows[2][0]=5, rows[2][1]=6 */
    assert(h.rows[0][0].val == 1 && h.rows[0][1].val == 2);
    assert(h.rows[1][0].val == 0 && h.rows[1][1].val == 0);
    assert(h.rows[2][0].val == 5 && h.rows[2][1].val == 6);

    /* t: grid[2][j][k] = 1..8、他は 0 */
    int expected[2][4] = {{1,2,3,4},{5,6,7,8}};
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 2; j++) {
            for (int k = 0; k < 4; k++) {
                if (i == 2) assert(t.grid[i][j][k].val == expected[j][k]);
                else assert(t.grid[i][j][k].val == 0);
            }
        }
    }

    return 0;
}
