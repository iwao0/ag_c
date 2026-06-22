/* グローバル struct の char 配列メンバを文字列リテラルで初期化 `struct S{char name[8];} g={"main"}`。
 * 以前は char[] メンバを char* と取り違え、文字列ラベルのアドレス (.quad .LC0) を 8 バイトに
 * 格納していたため name 全体がポインタ値に化けていた。global brace flat パーサが char 配列メンバ
 * (tag 無し・要素 1 バイト・array_len>0) の文字列を、ポインタでなく array_len バイトへ展開する
 * ように修正 (多次元 char 配列 `char g[2][6]` の行展開と同じ機構を struct メンバへ適用)。
 * メンバ要素サイズは tag_member_info の type_size から得る (char 配列メンバは deref_size=0)。
 * 注: char 配列メンバの後ろに char* メンバが続く形・2 次元 char メンバ・struct 配列内の char
 * メンバ は別の slot 相互作用が残り未対応 (HANDOFF 記載)。 */
#include <assert.h>

struct Cfg { int w, h; double scale; char name[8]; int flags[4]; };
struct Cfg g = { 640, 480, 1.5, "main", {1, 0, 1, 1} };

struct Esc { char m[6]; };
struct Esc ge = { "a\tb" };

struct Short { char tag[8]; };
struct Short gs = { "hi" };

int main(void) {
  assert(g.w == 640 && g.h == 480);
  assert((int)(g.scale * 10) == 15);
  assert(g.name[0] == 'm' && g.name[1] == 'a' && g.name[2] == 'i' && g.name[3] == 'n');
  assert(g.name[4] == 0 && g.name[7] == 0);
  assert(g.flags[0] == 1 && g.flags[1] == 0 && g.flags[2] == 1 && g.flags[3] == 1);

  /* エスケープを含む文字列 */
  assert(ge.m[0] == 'a' && ge.m[1] == '\t' && ge.m[2] == 'b' && ge.m[3] == 0);

  /* 短い文字列 + 0 埋め */
  assert(gs.tag[0] == 'h' && gs.tag[1] == 'i' && gs.tag[2] == 0 && gs.tag[7] == 0);

  /* ローカル struct (回帰防止) */
  struct L { char name[8]; int v; };
  struct L l = { "lo", 9 };
  assert(l.name[0] == 'l' && l.name[1] == 'o' && l.name[2] == 0 && l.v == 9);

  return 0;
}
