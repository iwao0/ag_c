/* グローバル struct の 3 次元 char 配列メンバ `struct{char c[2][2][3];} g={{{"ab","cd"},{"ef","gh"}}}`
 * を文字列リテラルで初期化し、その後 3 段 subscript でアクセスする。
 *
 * 以前は (1) gbrace_ctx_t が「行幅 (row_width)」しか持てず 3 次元以上で内側次元情報が落ち、
 * 各行文字列が要素 (char) 扱いされ array_len=0 でポインタ化 → SIGSEGV。
 * また (2) tag_member_info_t に mid_stride を持たず、3 段 subscript の 2/3 段目ストライドが
 * 立たないためアクセス側 (build_member_deref_node 経由) でも誤アドレスを deref → SIGSEGV。
 *
 * 修正: tag_member_info_t に arr_dims[8]/arr_ndim と mid_stride を追加。
 *  - 初期化側: gbrace_ctx_t に sub_dims チェーンを持たせ、gbrace_child_at が 1 段ずつ消費し
 *    最内側で 1 次元 char 配列 (= 文字列展開) を返す。
 *  - アクセス側: build_member_deref_node で 3 次元以上なら inner_deref_size=mid_stride,
 *    next_deref_size=elem_size を立て、ローカル多次元配列と同じ 3 段 stride 表現に乗せる。 */
#include <assert.h>

/* (1) 基本形 3D */
struct S { char c[2][2][3]; };
struct S g = { { { "ab", "cd" }, { "ef", "gh" } } };

/* (2) 3D char メンバ + 後続スカラ。初期化はネスト brace の最後にスカラを並べる。 */
struct T { char c[2][2][3]; int n; };
struct T gt = { { { "AB", "CD" }, { "EF", "GH" } }, 99 };

/* (3) 先行スカラ + 3D char メンバ。短い文字列の 0 埋めを確認。 */
struct U { int n; char c[2][2][4]; };
struct U gu = { 7, { { "hi", "yo" }, { "!", "" } } };

int main(void) {
  /* (1) 全 12 バイトを確認 */
  assert(g.c[0][0][0] == 'a' && g.c[0][0][1] == 'b' && g.c[0][0][2] == 0);
  assert(g.c[0][1][0] == 'c' && g.c[0][1][1] == 'd' && g.c[0][1][2] == 0);
  assert(g.c[1][0][0] == 'e' && g.c[1][0][1] == 'f' && g.c[1][0][2] == 0);
  assert(g.c[1][1][0] == 'g' && g.c[1][1][1] == 'h' && g.c[1][1][2] == 0);

  /* (2) 3D + 後続スカラ */
  assert(gt.c[0][0][0] == 'A' && gt.c[1][1][1] == 'H');
  assert(gt.n == 99);

  /* (3) 先行スカラ + 3D、短い行は 0 埋め (各行 4 バイト) */
  assert(gu.n == 7);
  assert(gu.c[0][0][0] == 'h' && gu.c[0][0][1] == 'i' && gu.c[0][0][2] == 0 && gu.c[0][0][3] == 0);
  assert(gu.c[0][1][0] == 'y' && gu.c[0][1][1] == 'o' && gu.c[0][1][3] == 0);
  assert(gu.c[1][0][0] == '!' && gu.c[1][0][1] == 0);
  assert(gu.c[1][1][0] == 0 && gu.c[1][1][3] == 0);

  return 0;
}
