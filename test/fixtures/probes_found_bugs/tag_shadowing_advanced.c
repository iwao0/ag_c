/* タグ shadowing の応用形 (変数の宣言時 scope_depth を覚える機構):
 * (a) ネスト 2 段 shadow: 内側 1 で宣言した変数を内側 2 から参照
 * (b) 内側スコープでグローバル変数 (外側 tag) のメンバを参照
 *
 * 以前は宣言と参照nodeが tag_kind/name/len しか持たず、宣言時の
 * tag_scope_depth を覚えていなかった。メンバ参照 (build_member_access) は find_tag_type で
 * 「最も内側」のタグを取得するため、変数の宣言時タグと参照時タグがズレ E3064 になっていた。
 *
 * 修正: declaration type のcanonical tag identityにscope depthを保持する。
 * build_member_access はcanonical typeからその深度を取り出し、
 * ps_ctx_find_tag_member_info_at_scope に渡して「変数が宣言時に見ていた tag」のメンバを
 * 引く。tag_scope_depth_p1 の +1 エンコードにより calloc/arena_alloc がそのまま「未設定」を
 * 意味し、既存ノードへの伝播忘れがあっても従来挙動 (最も内側 tag) に fallback する。 */
#include <assert.h>

/* (b) のセットアップ: 外側 struct S とそれを使うグローバル変数 */
struct S { int a; int b; };
struct S sg = { 7, 11 };

int main(void) {
  /* (a) ネスト 2 段 shadow: 内側 1 で s を宣言、内側 2 でさらに別 S を shadow し
   * 内側 2 から s を参照する。s.tag_scope_depth_p1 が内側 1 の深度を保持しているので、
   * find_tag_member_info_at_scope が内側 1 の S {int p,q,r;} のメンバを引ける。 */
  {
    struct S { int p; int q; int r; };
    struct S s = { 7, 8, 9 };
    {
      struct S { char c[4]; };  /* shadow */
      struct S cc = { { 'x', 'y', 'z', 0 } };
      assert(s.p == 7 && s.q == 8 && s.r == 9);
      assert(cc.c[0] == 'x' && cc.c[1] == 'y' && cc.c[2] == 'z' && cc.c[3] == 0);
    }
    /* 内側 2 を抜けたあとも s は引き続き内側 1 の S として動く。 */
    assert(s.p + s.q + s.r == 24);
  }

  /* (b) 内側スコープから外側 tag のグローバル変数のメンバを参照。
   * sg.tag_scope_depth_p1 = 1 (=外側 depth 0 + 1) が保存されているので、内側 S {double x,y;}
   * が見えていても sg.a / sg.b は外側 S のレイアウトで引ける。 */
  {
    struct S { double x; double y; };
    struct S s2 = { 1.5, 2.5 };
    assert(sg.a == 7 && sg.b == 11);
    assert(s2.x == 1.5 && s2.y == 2.5);
  }

  /* (c) 同じスコープ内で外側 → 内側 shadow → 内側変数参照 → 外側変数参照 */
  struct S sx = { 100, 200 };
  {
    struct S { int only; };
    struct S si = { 42 };
    assert(si.only == 42);
    assert(sx.a == 100 && sx.b == 200);  /* sx は外側 S として参照できる */
  }

  return 0;
}
