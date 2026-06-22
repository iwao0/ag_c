/* 関数内 (非 static) struct の 2 次元 char 配列メンバを文字列リテラルで初期化する。
 *   struct S{char rows[2][4];}; struct S l = {{"ab","cd"}};
 *
 * 以前は parse_member_initializer (decl.c) の多次元配列メンバ・ネスト brace 経路が
 * 各内側要素 ("ab" / "cd") を 1 個のスカラ値として読み (parse_scalar_brace_initializer)、
 * 文字列リテラルを `.LC0` ラベルアドレスの下位 1 バイトとして 1 slot に書き込んでいた
 * (`strb w20, [x19]`)。行 (4 バイト) として展開されないため値が化けていた。
 * グローバル経路 (psx_gbrace_flat) は別途修正済みだった (global_struct_2d_char_array_member)
 * が、ローカル経路は同じ機構を持っていなかった。
 *
 * 修正: parse_member_initializer の多次元配列メンバ・ネスト brace 経路で、要素として
 * 文字列リテラルが来たとき (elem_size==1) は文字列を inner_len バイトへバイト展開して
 * flat に書き込み、行ぶん進める (グローバル経路と対称な処理)。
 *
 * `{{"ab","cd"}}` 形式 (内側 brace あり、行ごとに 1 文字列) と、`{"ab","cd"}` 形式
 * (内側 brace 省略 = brace elision) の 2 形に対応。後者は parse_struct_initializer の
 * メンバ数判定 (member rows は 1 つ) が手前で `"cd"` を超過と判定し E3064 になるため、
 * ここでは前者形式のみテストする (brace elision は別経路の未対応で task 範囲外)。 */
#include <assert.h>

int main(void) {
  /* (1) 基本形 */
  struct S { char rows[2][4]; };
  struct S l = { { "ab", "cd" } };
  assert(l.rows[0][0] == 'a' && l.rows[0][1] == 'b' && l.rows[0][2] == 0 && l.rows[0][3] == 0);
  assert(l.rows[1][0] == 'c' && l.rows[1][1] == 'd' && l.rows[1][2] == 0 && l.rows[1][3] == 0);

  /* (2) 先行スカラ + 2D char メンバ */
  struct T { int n; char rows[2][4]; };
  struct T t = { 7, { "hi", "yo" } };
  assert(t.n == 7);
  assert(t.rows[0][0] == 'h' && t.rows[0][1] == 'i' && t.rows[0][2] == 0);
  assert(t.rows[1][0] == 'y' && t.rows[1][1] == 'o' && t.rows[1][2] == 0);

  /* (3) 短い文字列の 0 埋め (各行 5 バイト) */
  struct U { char rows[3][5]; };
  struct U u = { { "hi", "yo", "!" } };
  assert(u.rows[0][0] == 'h' && u.rows[0][4] == 0);
  assert(u.rows[1][0] == 'y' && u.rows[1][4] == 0);
  assert(u.rows[2][0] == '!' && u.rows[2][1] == 0 && u.rows[2][4] == 0);

  return 0;
}
