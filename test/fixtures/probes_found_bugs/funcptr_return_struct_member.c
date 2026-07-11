/* 関数ポインタ経由 (間接呼び出し) の戻り値 struct/union への直接メンバアクセス。
 * 以前は `op(41).v` が E3005 (`.` の左辺は構造体である必要があります) になっていた。
 * 直接呼び出し `mk(41).v` は動いていたが、間接呼び出しの funcall ノードに戻り tag
 * 型が伝播せず ps_node_get_tag_type が TK_EOF を返していたのが原因。
 * callee の funcptr 変数 (tag フィールドに戻り tag を保持) から導出し、戻り値が
 * ポインタか否かは pointer_qual_levels (値戻り=1 / ポインタ戻り=2) で判定する。 */
#include <assert.h>

/* 注: ここで使う struct は全て 1/2/4/8 バイト (レジスタ返し ABI)。1/2/4/8 以外の
 * サイズ (12B/20B 等、x8 ret_area 間接返し) を返す関数ポインタの間接呼び出しは
 * 別の既存バグ (IR build 失敗) のため対象外。 */
struct R { int v; };               /* 4B */
struct Pt { int x, y; };           /* 8B */
struct Wrap { struct Pt p; };      /* 8B: ネストメンバ連鎖の検証用 */
union U { int n; struct Pt q; };   /* 8B */

struct R   mkr(int x)  { struct R r;   r.v = x;            return r; }
struct Pt  mkpt(int s) { struct Pt r;  r.x = s; r.y = s*2; return r; }
struct Pt  gtbl;
struct Pt *mkpp(int s) { gtbl.x = s; gtbl.y = s + 1;       return &gtbl; }
struct Wrap mkw(int s) { struct Wrap r; r.p.x = s; r.p.y = s+1; return r; }
union U    mku(int s)  { union U r;    r.n = s;            return r; }

struct Pt (*gop)(int) = mkpt;   /* ファイルスコープ funcptr */

int main(void) {
  /* 値戻り struct: `.member` */
  struct R (*op)(int) = mkr;
  assert(op(41).v == 41);

  /* 複数メンバ + deref 形 `(*op)(s).member` */
  struct Pt (*opp)(int) = mkpt;
  assert(opp(10).x == 10);
  assert(opp(10).y == 20);
  assert((*opp)(7).y == 14);

  /* グローバル funcptr */
  assert(gop(5).x == 5);
  assert(gop(5).y == 10);

  /* ポインタ戻り struct: `op(s)->member` (pql=2) */
  struct Pt *(*opq)(int) = mkpp;
  assert(opq(8)->x == 8);
  assert(opq(8)->y == 9);

  /* 連鎖メンバ `op(s).inner.x` (8B ラッパー) */
  struct Wrap (*ow)(int) = mkw;
  assert(ow(3).p.x == 3);
  assert(ow(3).p.y == 4);

  /* union 戻り */
  union U (*ou)(int) = mku;
  assert(ou(9).n == 9);

  return 0;
}
