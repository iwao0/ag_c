// 続き75: グローバル struct メンバ多次元 struct タグ配列での内側 brace の designator (修正)。
// 続き74 の延長。
//
// 修正前: `.rows = {[2] = {{.val=99}}}` の内側 brace `{.val=99}` で「メンバ指定初期化子が
// 不正です (実際のトークン: '=')」E3064。内側 `[1] = {...}` 形 (例: `[2] = {[1] = {.val=99}}`)
// も同じく拒否。
//
// 原因: parser.c の gbrace_child_at が「sub_dims を 1 段消費して内側 ctx を生成する」処理を
// `ctx.tag_kind == TK_EOF` 限定 (タグ無し多次元配列のみ) としていたため、struct タグ多次元配列
// の内側 brace ctx が「単一 struct」になり、内側 designator が「単一要素なのに添字/メンバ
// 指定?」として弾かれていた。
//
// 修正: gbrace_child_at の sub_dims 処理を tag_kind 非依存にし、struct タグ配列でも中間段 /
// 最内 1 段で「内側次元の配列 (タグ継承)」ctx を返すよう拡張。これで `{[2] = {{.val=99}}}` の
// 中の `{.val=99}` は struct Cell 単一要素の ctx で `.val=` を解決可能、`[2]={[1]={.val=99}}`
// の中の `[1]` も配列 1 段添字として解決可能。
//
// ローカル `struct Grid g = {.rows={[2]={[1]={.val=99}}}};` は別経路
// (local_struct_member_multidim_nested_designator) で対応済み。
#include <assert.h>

struct Cell { int val; char tag; };
struct Grid { struct Cell rows[3][2]; };
struct Big { struct Cell cube[2][3][2]; };

/* 内側 brace で `.member=` designator */
struct Grid g1 = {.rows = {[2] = {{.val=99, .tag='Z'}}}};

/* 内側 brace で `[N]=` designator */
struct Grid g2 = {.rows = {[2] = {[1] = {.val=88, .tag='Y'}}}};

/* p12 元 probe 同形 (ネストした 2 段 designator + 部分初期化) */
struct Grid g3 = {.rows = {
    [0] = {{.val=1, .tag='A'}, {.val=2, .tag='B'}},
    [2] = {[1] = {.val=99, .tag='Z'}}
}};

/* 3D struct タグ配列 + 内側 designator (cube[2][3][2]: 3 行 * 2 列 / 行) */
struct Big b = {.cube = {[1] = {{{.val=10}, {.val=20}},
                                 {{.val=30}, {.val=40}},
                                 {{.val=50}, {.val=60}}}}};

int main(void) {
    /* g1: rows[2][0] = {99,'Z'}、他は 0 */
    assert(g1.rows[0][0].val == 0 && g1.rows[0][0].tag == 0);
    assert(g1.rows[2][0].val == 99 && g1.rows[2][0].tag == 'Z');
    assert(g1.rows[2][1].val == 0 && g1.rows[2][1].tag == 0);

    /* g2: rows[2][1] = {88,'Y'}、他は 0 */
    assert(g2.rows[2][0].val == 0 && g2.rows[2][0].tag == 0);
    assert(g2.rows[2][1].val == 88 && g2.rows[2][1].tag == 'Y');

    /* g3: rows[0]={{1,A},{2,B}}, rows[1]={0,0}, rows[2]={0,{99,Z}} */
    assert(g3.rows[0][0].val == 1 && g3.rows[0][0].tag == 'A');
    assert(g3.rows[0][1].val == 2 && g3.rows[0][1].tag == 'B');
    assert(g3.rows[1][0].val == 0 && g3.rows[1][1].val == 0);
    assert(g3.rows[2][0].val == 0 && g3.rows[2][0].tag == 0);
    assert(g3.rows[2][1].val == 99 && g3.rows[2][1].tag == 'Z');

    /* b: cube[1] が 6 要素 (10..60)、他は 0 */
    assert(b.cube[0][0][0].val == 0);
    assert(b.cube[1][0][0].val == 10 && b.cube[1][0][1].val == 20);
    assert(b.cube[1][1][0].val == 30 && b.cube[1][1][1].val == 40);
    assert(b.cube[1][2][0].val == 50 && b.cube[1][2][1].val == 60);

    return 0;
}
