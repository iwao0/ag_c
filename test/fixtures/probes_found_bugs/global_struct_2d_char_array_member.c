/* グローバル struct の 2 次元 char 配列メンバ `struct{char rows[2][4];} g={{"ab","cd"}}` を
 * 各行の文字列リテラルで初期化する。
 *
 * 以前は 2 次元 char メンバの行幅 (outer_stride) が global brace flat パーサのコンテキスト
 * (gbrace_ctx_t) に伝わらず、ネスト brace `{"ab","cd"}` の各行文字列が「要素 (char)」扱いされ
 * array_len=0 でポインタ (.LC ラベル) として出力されていた (`.quad .LC0; .quad .LC1`)。
 * 結果、行データがポインタ値に化けていた。
 *
 * 修正: gbrace_ctx_t に row_width を追加し、多次元 char 配列メンバ (tag 無し・要素 1 バイト・
 * outer_stride>0・array_len>outer_stride) では outer_stride を行幅として持たせる。
 * gbrace_child_at が各要素を「内側 1 次元 char 配列 (char[row_width])」として返すので、
 * 既存の char 配列メンバ展開分岐に乗り、行ごとに row_width バイトへ展開される。
 *
 * 3 次元以上の char メンバは global_struct_3d_char_array_member.c、
 * ローカル (非 static) struct の 2/3 次元 char メンバは
 * local_struct_2d_char_array_member.c / local_struct_3d_char_array_member.c で対応済み。 */
#include <assert.h>

/* (1) 基本形 */
struct S { char rows[2][4]; };
struct S g = { { "ab", "cd" } };

/* (2) 2D char メンバ + 後続スカラメンバ */
struct T { char rows[2][4]; int n; };
struct T gt = { { "xy", "zw" }, 42 };

/* (3) スカラメンバが先、その後 2D char メンバ。短い文字列は 0 埋め。 */
struct U { int n; char rows[3][5]; };
struct U gu = { 9, { "hi", "yo", "!" } };

int main(void) {
  assert(g.rows[0][0] == 'a' && g.rows[0][1] == 'b' && g.rows[0][2] == 0 && g.rows[0][3] == 0);
  assert(g.rows[1][0] == 'c' && g.rows[1][1] == 'd' && g.rows[1][2] == 0 && g.rows[1][3] == 0);

  assert(gt.rows[0][0] == 'x' && gt.rows[1][0] == 'z' && gt.rows[1][1] == 'w');
  assert(gt.n == 42);

  assert(gu.n == 9);
  assert(gu.rows[0][0] == 'h' && gu.rows[0][1] == 'i' && gu.rows[0][2] == 0);
  assert(gu.rows[1][0] == 'y' && gu.rows[1][1] == 'o');
  assert(gu.rows[2][0] == '!' && gu.rows[2][1] == 0 && gu.rows[2][4] == 0);

  return 0;
}
