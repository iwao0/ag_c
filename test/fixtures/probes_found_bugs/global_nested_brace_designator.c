/* グローバル集約のネスト brace 内 designator (C11 6.7.9)。
 * `struct C{struct I items[N];}; g={.items={[2]={.a=7}}}` の内側 `.a` を最外 struct C の
 * 型に対して解決して E3064 にしていた。flat 初期化パーサがネスト brace の再帰に「その
 * brace level が初期化している集約型」コンテキストを渡しておらず、designator を常に gv の
 * tag で解決していたのが原因。型コンテキストを再帰へ渡し、ネスト配列の [N]= 絶対 slot 計算
 * (level 起点を加算) と、配列レベル positional 要素の境界整列も併せて修正。 */
#include <assert.h>

struct I { int a, b; };
struct P { int x; struct I s; };          /* ネスト struct メンバ */
union  U { int n; long l; };
struct D { double d; int k; };

/* (1) 配列メンバ + ネスト配列添字 designator */
struct C1 { int n; struct I items[4]; };
struct C1 g1 = {.n = 1, .items = {[2] = {.a = 7, .b = 8}}};

/* (2) 複数の配列添字 designator */
struct C2 { struct I items[4]; };
struct C2 g2 = {.items = {[1] = {.a = 1}, [3] = {.b = 2}}};

/* (3) positional 要素 + 内側 designator (部分初期化を含む) */
struct C2 g3 = {.items = {{.a = 10}, {.b = 20}}};

/* (4) designator と positional の混在 */
struct C3 { struct I items[3]; };
struct C3 g4 = {.items = {[1] = {.a = 3}, {.b = 4}}};   /* [1] の次は要素2 */

/* (5) ネスト struct メンバ designator (配列でない) */
struct Q { struct I s; };
struct Q g5 = {.s = {.a = 9}};

/* (6) 二段ネスト .member.sub designator チェーン */
struct P g6 = {.x = 5, .s = {.b = 6}};

/* (7) fp メンバを含むネスト */
struct E { struct D arr[2]; };
struct E g7 = {.arr = {[1] = {.d = 2.5, .k = 3}}};

int main(void) {
  assert(g1.n == 1 && g1.items[2].a == 7 && g1.items[2].b == 8);
  assert(g1.items[0].a == 0 && g1.items[3].b == 0);   /* 隙間は 0 */
  assert(g2.items[1].a == 1 && g2.items[3].b == 2 && g2.items[2].a == 0);
  assert(g3.items[0].a == 10 && g3.items[0].b == 0);
  assert(g3.items[1].a == 0 && g3.items[1].b == 20);  /* 部分初期化の境界整列 */
  assert(g4.items[1].a == 3 && g4.items[2].b == 4 && g4.items[0].a == 0);
  assert(g5.s.a == 9 && g5.s.b == 0);
  assert(g6.x == 5 && g6.s.a == 0 && g6.s.b == 6);
  assert(g7.arr[1].d == 2.5 && g7.arr[1].k == 3 && g7.arr[0].d == 0.0);
  return 0;
}
