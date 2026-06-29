// 続き76: ローカル struct の多次元 struct タグ配列メンバで designator init (修正)。
// 続き75 のグローバル版に続く、ローカル経路 (parse_member_initializer) の対応。
//
// 修正前: 関数内で `struct Grid g = {.rows = {[2]={[1]={.val=99}}}};` のような designator が
// 「E3064: [primary] 数値が必要です (実際のトークン: '[')」で拒否されていた。parse_member_initializer
// の outer_stride 経路 (多次元配列メンバ) が `[N]=` designator を全く扱わず、`[` を見ると
// parse_scalar_brace_initializer → primary() に投げて失敗。また struct 要素も
// parse_scalar_brace_initializer で 1 値として読むため、struct 内の `.member=` も扱えなかった。
//
// 修正: parse_member_initializer の outer_stride 経路で
//   (a) 外側 brace 開始時に `[N]=` を検出して flat = N * inner_len にジャンプ、
//   (b) 内側 brace 開始時に `[M]=` を検出して flat = row*inner_len + M にジャンプ、
//   (c) 要素 tag が struct/union の場合は nested lvar を作って parse_struct_initializer で
//       1 slot ぶん解釈する (`{.val=99}` / `{99, 'A'}` などを正しく扱う)。
//
// 続き: 3D 以上では `cube[2][3][2]` の中間 brace を「次元 level」ではなく struct 要素
// として早く解釈し、`{{{.val=10},...}}` の内側 `.val` で E3064 になっていた。
// 3D 以上の struct/union タグ配列メンバは次元ごとに再帰し、最下層だけを
// parse_struct_initializer / parse_union_initializer へ委譲する。
//
// 単一フィールド struct (positional `{{1},{2},...}`) の既存挙動は不変。
#include <assert.h>

struct Cell { int val; char tag; };
struct Grid { struct Cell rows[3][2]; };
struct Big { struct Cell cube[2][3][2]; };

int main(void) {
    /* ローカル + 外側 designator + 内側 positional */
    struct Grid a = {.rows = {[2] = {{99, 'X'}, {100, 'Y'}}}};
    assert(a.rows[0][0].val == 0 && a.rows[1][1].val == 0);
    assert(a.rows[2][0].val == 99 && a.rows[2][0].tag == 'X');
    assert(a.rows[2][1].val == 100 && a.rows[2][1].tag == 'Y');

    /* ローカル + 内側 designator のみ (外側 positional) */
    struct Grid b = {.rows = {{[1] = {77, 'Q'}}, {[0] = {66, 'P'}}}};
    assert(b.rows[0][0].val == 0 && b.rows[0][1].val == 77 && b.rows[0][1].tag == 'Q');
    assert(b.rows[1][0].val == 66 && b.rows[1][0].tag == 'P' && b.rows[1][1].val == 0);
    assert(b.rows[2][0].val == 0);

    /* ローカル + 外側 designator + 内側 designator + struct 内の `.member=` */
    struct Grid c = {.rows = {[2] = {[1] = {.val = 88, .tag = 'Z'}}}};
    assert(c.rows[0][0].val == 0 && c.rows[1][0].val == 0);
    assert(c.rows[2][0].val == 0 && c.rows[2][0].tag == 0);
    assert(c.rows[2][1].val == 88 && c.rows[2][1].tag == 'Z');

    /* p12 元 probe (ローカル化、struct 内 `.val=`/`.tag=` 混在) */
    struct Grid d = {.rows = {
        [0] = {{.val=1, .tag='A'}, {.val=2, .tag='B'}},
        [2] = {[1] = {.val=99, .tag='Z'}}
    }};
    assert(d.rows[0][0].val == 1 && d.rows[0][0].tag == 'A');
    assert(d.rows[0][1].val == 2 && d.rows[0][1].tag == 'B');
    assert(d.rows[1][0].val == 0 && d.rows[1][1].val == 0);
    assert(d.rows[2][0].val == 0 && d.rows[2][0].tag == 0);
    assert(d.rows[2][1].val == 99 && d.rows[2][1].tag == 'Z');

    /* 単一フィールド struct での positional は既存挙動を維持 (回帰確認) */
    struct Cell2 { int v; };
    struct Grid2 { struct Cell2 r[3][2]; };
    struct Grid2 e = {.r = {{{1},{2}}, {{3},{4}}, {{5},{6}}}};
    assert(e.r[0][0].v == 1 && e.r[1][1].v == 4 && e.r[2][1].v == 6);

    /* 3D struct タグ配列 + 中間 level の brace + 最下層 `.member=` */
    struct Big f = {.cube = {[1] = {{{.val=10}, {.val=20}},
                                      {{.val=30}, {.val=40}},
                                      {{.val=50}, {.val=60}}}}};
    assert(f.cube[0][0][0].val == 0);
    assert(f.cube[1][0][0].val == 10 && f.cube[1][0][1].val == 20);
    assert(f.cube[1][1][0].val == 30 && f.cube[1][1][1].val == 40);
    assert(f.cube[1][2][0].val == 50 && f.cube[1][2][1].val == 60);

    return 0;
}
