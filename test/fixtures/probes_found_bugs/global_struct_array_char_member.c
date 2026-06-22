/* グローバル struct 配列の要素メンバに char 配列がある初期化
 * `struct S{char tag[4]; int n;} g[2]={{"aa",1},{"bb",2}}`。
 *
 * 以前は emit_global_struct_array_init がメンバごとにフラット slot を 1 個だけ消費する
 * 単純ループだったため、配列メンバ (`char tag[4]`)・char 配列の文字列展開・入れ子 struct
 * メンバ・bitfield を扱えなかった。tag は 1 バイトしか出力されず (.byte 97 / .space 3)、
 * 後続メンバ n に 2 文字目の 'a' (97) が入り値が総崩れになっていた。
 *
 * 修正: emit_global_struct_array_init を各要素について emit_global_struct_members_rec を
 * 呼ぶ形に書き換え、非配列 struct (emit_global_struct_init) と同じメンバ展開機構を要素
 * ごとに適用して統一した (配列メンバ/char 配列展開/入れ子 struct/bitfield/部分初期化の
 * ゼロ埋めを共通処理)。parser 側 (psx_gbrace_flat) は元から各要素の char 配列メンバを
 * バイト展開できていた (emit 側のみの不具合)。 */
#include <assert.h>

/* (1) char 配列メンバが先頭 + 後続スカラ */
struct S { char tag[4]; int n; };
struct S g[2] = { { "aa", 1 }, { "bb", 2 } };

/* (2) スカラが先頭 + char 配列メンバ */
struct R { int n; char tag[4]; };
struct R gr[2] = { { 1, "xy" }, { 2, "zw" } };

/* (3) 部分初期化: 残り要素はゼロ埋め */
struct S gp[3] = { { "aa", 7 } };

/* (4) 入れ子 struct メンバ + char 配列メンバ */
struct P { int x, y; };
struct Q { struct P p; char tag[4]; };
struct Q gq[2] = { { { 1, 2 }, "ab" }, { { 3, 4 }, "cd" } };

/* (5) char 配列メンバ 2 本 */
struct W { char a[4]; char b[4]; };
struct W gw[2] = { { "ab", "cd" }, { "ef", "gh" } };

int main(void) {
  assert(g[0].tag[0] == 'a' && g[0].tag[1] == 'a' && g[0].tag[2] == 0 && g[0].n == 1);
  assert(g[1].tag[0] == 'b' && g[1].tag[1] == 'b' && g[1].tag[2] == 0 && g[1].n == 2);

  assert(gr[0].n == 1 && gr[0].tag[0] == 'x' && gr[0].tag[1] == 'y' && gr[0].tag[2] == 0);
  assert(gr[1].n == 2 && gr[1].tag[0] == 'z' && gr[1].tag[1] == 'w');

  assert(gp[0].tag[0] == 'a' && gp[0].n == 7);
  assert(gp[1].tag[0] == 0 && gp[1].n == 0);
  assert(gp[2].tag[0] == 0 && gp[2].n == 0);

  assert(gq[0].p.x == 1 && gq[0].p.y == 2 && gq[0].tag[0] == 'a' && gq[0].tag[1] == 'b');
  assert(gq[1].p.x == 3 && gq[1].p.y == 4 && gq[1].tag[0] == 'c' && gq[1].tag[1] == 'd');

  assert(gw[0].a[0] == 'a' && gw[0].b[0] == 'c' && gw[1].a[1] == 'f' && gw[1].b[1] == 'h');

  return 0;
}
