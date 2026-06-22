/* 関数内 (非 static) struct の 3 次元 char 配列メンバを文字列リテラルで初期化する。
 *   struct S{char c[2][2][3];}; struct S l = {{{"ab","cd"},{"ef","gh"}}};
 *
 * 以前は parse_member_initializer (decl.c) の多次元 brace 経路が outer_stride 1 つ
 * (= 1 段目 subscript 後のバイト数 6) しか持たず、行自体がさらに 2D 配列となる 3D
 * 以上で内側次元構造を見れなかった。文字列 "ab" / "cd" を「行 (6 バイト)」として
 * 一気に展開し、内側の `[2][3]` レイアウト (1 文字列 = 3 バイトずつ) を無視して
 * 詰め込んでいたため値が崩れていた。グローバル経路は続き10 で arr_dims/mid_stride を
 * 追加して対応済みだったが、ローカル経路は別実装。
 *
 * 修正: parse_member_initializer に member_arr_dims / member_arr_ndim を渡し、3D 以上
 * (member_arr_ndim>=3) なら新しい再帰関数 parse_multidim_char_member_brace に委譲する。
 * グローバルの gbrace_ctx_t.sub_dims チェーンと同等の機構で、最内 1 次元を char 行
 * (文字列展開) として扱い、ndim>=2 は brace ごとに 1 段消費して再帰する。 */
#include <assert.h>

int main(void) {
  /* (1) 基本形 3D */
  struct S { char c[2][2][3]; };
  struct S l = { { { "ab", "cd" }, { "ef", "gh" } } };
  assert(l.c[0][0][0] == 'a' && l.c[0][0][1] == 'b' && l.c[0][0][2] == 0);
  assert(l.c[0][1][0] == 'c' && l.c[0][1][1] == 'd' && l.c[0][1][2] == 0);
  assert(l.c[1][0][0] == 'e' && l.c[1][0][1] == 'f' && l.c[1][0][2] == 0);
  assert(l.c[1][1][0] == 'g' && l.c[1][1][1] == 'h' && l.c[1][1][2] == 0);

  /* (2) 3D + 後続スカラ */
  struct T { char c[2][2][3]; int n; };
  struct T t = { { { "AB", "CD" }, { "EF", "GH" } }, 99 };
  assert(t.c[0][0][0] == 'A' && t.c[1][1][1] == 'H');
  assert(t.n == 99);

  /* (3) 先行スカラ + 3D、短い文字列の 0 埋め (各行 4 バイト) */
  struct U { int n; char c[2][2][4]; };
  struct U u = { 7, { { "hi", "yo" }, { "!", "" } } };
  assert(u.n == 7);
  assert(u.c[0][0][0] == 'h' && u.c[0][0][1] == 'i' && u.c[0][0][2] == 0 && u.c[0][0][3] == 0);
  assert(u.c[0][1][0] == 'y' && u.c[0][1][1] == 'o' && u.c[0][1][3] == 0);
  assert(u.c[1][0][0] == '!' && u.c[1][0][1] == 0);
  assert(u.c[1][1][0] == 0 && u.c[1][1][3] == 0);

  return 0;
}
