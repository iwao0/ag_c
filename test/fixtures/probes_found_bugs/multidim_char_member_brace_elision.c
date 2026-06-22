/* 多次元 char 配列メンバへの brace elision (C11 6.7.9p20):
 *   struct B{char rows[2][4];}; struct B b = {"ab","cd"};
 * 外側 brace 内に文字列を直接並べ、各文字列を 1 行として扱う形。
 *
 * 以前は 2 系統で同時に壊れていた:
 * (グローバル) psx_gbrace_flat の TK_STRING 分岐が row_w = child.array_len (= メンバ全要素数) を
 *   使い、1 つ目の文字列で配列全体を埋め、2 つ目の文字列が「次メンバ」として扱われていた
 *   (struct に他メンバが無いと 0 埋めだけ)。
 * (ローカル) parse_struct_initializer のループが 1 メンバ分のみ try_parse_array_member_string_initializer
 *   に渡し、最初の文字列で配列全体を埋めて return、2 つ目の "cd" は E3064 で診断していた。
 *
 * 修正:
 * (グローバル) child.sub_ndim>0 のとき row_w = child.sub_dims[最後] (= 行幅) を使い 1 行ぶん消費
 *   する。次反復で gbrace_child_at が同じメンバを返し、cur_idx を行幅ぶん進めるだけで次行が
 *   自然に処理される (構造体内の slot 位置に従って member rows がまだ続いているため)。
 * (ローカル) parse_member_initializer に brace elision 専用分岐を追加 (arr_ndim>=2, elem_size==1,
 *   curtok==TK_STRING)。行ごとに文字列を消費し、`,文字列` が続く限りループ。 */
#include <assert.h>

/* (1) グローバル 2D brace elision */
struct B { char rows[2][4]; };
struct B g = { "ab", "cd" };

/* (2) グローバル 3D brace elision (全 brace 省略、各文字列 = 最内 1D 行) */
struct A { char c[2][2][3]; };
struct A ga = { "ab", "cd", "ef", "gh" };

/* (3) スカラ先頭 + 2D brace elision */
struct U { int n; char rows[2][4]; };
struct U gu = { 9, "hi", "yo" };

int main(void) {
  /* (1) グローバル 2D */
  assert(g.rows[0][0] == 'a' && g.rows[0][1] == 'b' && g.rows[0][2] == 0 && g.rows[0][3] == 0);
  assert(g.rows[1][0] == 'c' && g.rows[1][1] == 'd' && g.rows[1][2] == 0 && g.rows[1][3] == 0);

  /* (2) グローバル 3D */
  assert(ga.c[0][0][0] == 'a' && ga.c[0][0][1] == 'b' && ga.c[0][0][2] == 0);
  assert(ga.c[0][1][0] == 'c' && ga.c[1][0][0] == 'e' && ga.c[1][1][0] == 'g');

  /* (3) スカラ先頭 + 2D brace elision (グローバル) */
  assert(gu.n == 9);
  assert(gu.rows[0][0] == 'h' && gu.rows[1][0] == 'y' && gu.rows[1][1] == 'o' && gu.rows[1][3] == 0);

  /* (4) ローカル 2D brace elision */
  struct L { char rows[2][4]; };
  struct L l = { "ab", "cd" };
  assert(l.rows[0][0] == 'a' && l.rows[0][1] == 'b' && l.rows[0][2] == 0 && l.rows[0][3] == 0);
  assert(l.rows[1][0] == 'c' && l.rows[1][1] == 'd' && l.rows[1][2] == 0 && l.rows[1][3] == 0);

  /* (5) ローカル 3D brace elision */
  struct M { char c[2][2][3]; };
  struct M lm = { "AB", "CD", "EF", "GH" };
  assert(lm.c[0][0][0] == 'A' && lm.c[0][1][0] == 'C');
  assert(lm.c[1][0][0] == 'E' && lm.c[1][1][0] == 'G');

  return 0;
}
