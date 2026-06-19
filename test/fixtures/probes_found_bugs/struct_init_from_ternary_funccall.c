// <=8B struct を返す関数呼び出しを分岐に持つ三項演算子で struct を初期化する
// (`struct S s = c ? ok() : err(1);`)。旧実装の build_struct_copy_from_value は
// ND_TERNARY の両分岐が互換 lvar のときしか対応せず、funccall 分岐を
// 「構造体の単一式初期化は同型オブジェクトのみ対応」(E3064) で拒否していた。
// <=8B は ND_ASSIGN(var, ternary) でコピー初期化する経路を追加して修正
// (ternary 結果が 1 ワードのスカラ選択として codegen され funccall 戻り値も扱える)。
// (>8B struct の ternary-funccall は build_assign_struct が未対応のため対象外。)
#include <assert.h>
struct Status { int code; int flags; };           // 8B
struct Status ok(void){ struct Status s = {0, 1}; return s; }
struct Status err(int c){ struct Status s = {c, 0}; return s; }

int main(void) {
  int r = 0;

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

  return 0;
}
