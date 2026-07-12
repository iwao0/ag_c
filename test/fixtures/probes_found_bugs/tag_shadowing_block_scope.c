/* 内側ブロックで同名 struct/union を別レイアウトで宣言できる (C11 6.2.1: スコープ別の名前空間)。
 *   struct S {int a; int b;};
 *   { struct S {double x; double y;}; struct S s = {1.5, 2.5}; ... }
 *   /* この後は外側 S が見える *\/
 *
 * 以前は ps_ctx_define_tag_type_with_layout が「既存タグがあれば update して return」と
 * なっており、内側スコープで同名タグを宣言しても外側タグが上書きされていた (実際は同じ
 * member_count なら update せず存在を継続し、内側変数のメンバ解決が外側タグのレイアウトで
 * 行われていた)。結果、s.x への代入が外側 S のメンバ a/b の位置に入って値が崩壊。
 * 外側に戻った後の struct S s3 も影響を受けて壊れていた。
 *
 * 修正: define で existing->scope_depth と tag_scope_depth が一致するときだけ update。
 * 異なれば新規挿入 (先頭に push)、leave_block_scope で内側エントリは削除される。
 * get/find_tag_member_info で「対象 tag の scope_depth と一致するメンバのみ列挙/検索」する。
 *
 * 後続修正で、ネスト 2 段以上の shadow 中に外側/中間 scope で宣言した変数を参照する形も
 * 変数の宣言時 tag scope を使って解決できるようになった。 */
#include <assert.h>

/* (1) 外側 struct S */
struct S { int a; int b; };

int sum_outer(struct S s) { return s.a + s.b; }
struct S gs = { 11, 12 };

int main(void) {
  /* (2) 外側 S を使う (基本動作確認) */
  struct S s1 = { 1, 2 };
  assert(sum_outer(s1) == 3);

  /* (3) 内側ブロックで shadow: 別レイアウト struct S */
  int inner_x_x10 = 0, inner_y_x10 = 0;
  {
    struct S { double x; double y; };
    struct S s2 = { 1.5, 2.5 };
    inner_x_x10 = (int)(s2.x * 10);
    inner_y_x10 = (int)(s2.y * 10);
  }
  assert(inner_x_x10 == 15);
  assert(inner_y_x10 == 25);

  /* (4) 外側 S に戻った後の新規変数 */
  struct S s3 = { 10, 20 };
  assert(sum_outer(s3) == 30);

  /* (5) 同じスコープ内で外側 S → 内側 S → 外側 S の往復 */
  struct S s4 = { 7, 8 };
  assert(s4.a == 7 && s4.b == 8);
  {
    struct S { int z; };
    struct S si = { 99 };
    assert(si.z == 99);
  }
  struct S s5 = { 50, 60 };
  assert(s5.a == 50 && s5.b == 60);

  /* (6) 2 段 shadow の内側から中間 scope 変数と外側 tag の global を参照 */
  int seen_mid = 0;
  int seen_global = 0;
  {
    struct S { int mid; int pad; };
    struct S sm = { 2, 20 };
    {
      struct S { double inner; };
      struct S si2 = { 3.5 };
      seen_mid = sm.mid + sm.pad;
      seen_global = gs.a + gs.b;
      assert((int)(si2.inner * 10) == 35);
    }
  }
  assert(seen_mid == 22);
  assert(seen_global == 23);

  return 0;
}
