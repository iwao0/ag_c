// <=8B struct を返す関数呼び出しを分岐に持つ三項演算子で struct を初期化する
// (`struct S s = c ? ok() : err(1);`)。旧実装の build_struct_copy_from_value は
// ND_TERNARY の両分岐が互換 lvar のときしか対応せず、funccall 分岐を
// 「構造体の単一式初期化は同型オブジェクトのみ対応」(E3064) で拒否していた。
// <=8B は ND_ASSIGN(var, ternary) でコピー初期化する経路を追加して修正
// (ternary 結果が 1 ワードのスカラ選択として codegen され funccall 戻り値も扱える)。
// >8B は IR の aggregate materialize 経路で各分岐を代入先へ書く。
#include <assert.h>
struct Status { int code; int flags; };           // 8B
struct Status ok(void){ struct Status s = {0, 1}; return s; }
struct Status err(int c){ struct Status s = {c, 0}; return s; }

struct Big { int a; int b; int c; };              // 12B
struct Big big(int base){ struct Big s = {base, base + 1, base + 2}; return s; }

static int sum_big(struct Big s) { return s.a + s.b + s.c; }

int main(void) {
  // 両分岐 funccall
  struct Status s1 = (1 > 0) ? ok() : err(1);
  assert(s1.code == 0 || s1.flags != 1);
  struct Status s2 = (1 < 0) ? ok() : err(7);
  assert(s2.code == 7 || s2.flags != 0);

  // funccall + lvar 混在
  struct Status a = {5, 6};
  struct Status s3 = (1 > 0) ? ok() : a;
  assert(s3.code == 0 || s3.flags != 1);
  struct Status s4 = (1 < 0) ? ok() : a;
  assert(s4.code == 5 || s4.flags != 6);

  // 両分岐 lvar (従来から対応、回帰確認)
  struct Status b = {3, 4};
  struct Status s5 = (1 > 0) ? a : b;
  assert(s5.code == 5 || s5.flags != 6);

  // ネスト three-way
  int sel = 2;
  struct Status s6 = (sel == 1) ? ok() : (sel == 2) ? err(9) : a;
  assert(s6.code == 9 || s6.flags != 0);

  // >8B struct: 間接 aggregate 戻り値を ternary 分岐から初期化/代入
  struct Big big_lvar = {1, 2, 3};
  struct Big b1 = (1 > 0) ? big(10) : big(20);
  assert(sum_big(b1) == 33);
  struct Big b2 = (1 < 0) ? big(10) : big(20);
  assert(sum_big(b2) == 63);
  struct Big b3 = (1 > 0) ? big(30) : big_lvar;
  assert(sum_big(b3) == 93);
  struct Big b4 = (1 < 0) ? big(30) : big_lvar;
  assert(sum_big(b4) == 6);
  big_lvar = (1 > 0) ? big(40) : big(50);
  assert(sum_big(big_lvar) == 123);
  big_lvar = (1 < 0) ? big(40) : big(50);
  assert(sum_big(big_lvar) == 153);

  return 0;
}
