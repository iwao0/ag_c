/* fp 宣言 (`typedef long double` / `double` グローバル) の直後に来る tag グローバルが、
 * 前の宣言の decl-spec fp_kind を引き継いでしまう汚染バグの回帰テスト。
 *
 * 原因: 旧トップレベル dispatcherは tag キーワード始まりの宣言を
 * parse_toplevel_tag_decl へ直接回し、その前に reset_toplevel_decl_spec_state を呼ばな
 * かった。そのため g_toplevel_decl_fp_kind が前宣言 (例: `typedef long double max_align_t;`
 * = stddef.h 経由で string.h 等から間接 include) の DOUBLE のまま残り、ここで宣言する
 * struct/union object の fp_kind が DOUBLE になっていた。すると global brace init の
 * fp-fold 経路 (gv->fp_kind != NONE) が文字列リテラル/関数参照/アドレス初期化子を fp 定数
 * (0) として食べ、後続メンバが NULL / 0 に化けた (`{char b[4]; char*p;}` の p が .quad 0)。
 *
 * 修正: parse_toplevel_tag_decl の冒頭で reset_toplevel_decl_spec_state を呼び、宣言ごとに
 * decl-spec 状態を全クリアする (tag 情報は後段の install_toplevel_tag_decl_globals が再設定)。
 *
 * HANDOFF の「char[] メンバの後に char* メンバ」サブケース (a) の真因はこの汚染であり、
 * 先行 fp 宣言が無ければ char[]+char* の組合せ自体は元から正しく動作していた。 */
#include <assert.h>

/* 汚染源: tag グローバルの直前に fp 宣言を置く。 */
typedef long double md_t;

/* (1) char[] メンバの後に char* メンバ: p が .quad 0 (NULL) に化けていた。 */
struct S { char buf[4]; char *p; };
struct S g = { "ab", "cd" };

/* (2) 関数参照初期化子も fp-fold に食われて 0 になっていた。 */
int sq(int x) { return x * x; }
struct Op { int (*f)(int); };
struct Op gop = { sq };

/* (3) アドレス初期化子も同様 (data 配列要素の &)。 */
int data[3] = { 10, 20, 30 };
struct P { int *q; };
struct P gp = { &data[1] };

/* (4) スカラの char* グローバル (struct でなくても tag 経路は通らないが、fp 汚染が無いことの確認)。 */
double dummy_fp;
char *msg = "hi";

int main(void) {
  /* char[] メンバは正しく展開され、後続 char* メンバはラベルを保持する。 */
  assert(g.buf[0] == 'a' && g.buf[1] == 'b' && g.buf[2] == 0 && g.buf[3] == 0);
  assert(g.p[0] == 'c' && g.p[1] == 'd' && g.p[2] == 0);

  assert(gop.f(6) == 36);

  assert(gp.q[0] == 20);

  assert(msg[0] == 'h' && msg[1] == 'i' && msg[2] == 0);

  return 0;
}
