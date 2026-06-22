/* グローバル struct 内の多次元配列メンバへのネスト designator:
 *   struct P{int x[3][3];}; struct P p = {.x = {[0]={1,2,3}, [2]={[2]=9}}};
 *
 * 以前は psx_gbrace_flat の `[N]=` 経路が「scalar 要素配列」(elem_slots=1) として
 * 処理し、`[2]=` が slot 6 ではなく slot 2 へジャンプしていた (`int x[3][3]` は
 * 1 要素=3 slot)。そのため `[2]={[2]=9}` の 9 が p.x[2][2] (slot 8) ではなく p.x[1][1]
 * (slot 4) に書かれていた。char 多次元メンバへの修正 (続き10) で導入した
 * arr_dims/sub_dims 機構は char 限定 (`elem_size==1`) だったため非 char には適用されず。
 *
 * 修正: struct_layout で多次元配列メンバの arr_dims 保存を char 限定から外し、
 * gbrace_ctx_from_member / gbrace_child_at の sub_dims 機構を非 char にも一般化。
 * psx_gbrace_flat の `[N]=` 経路で ctx.sub_ndim>0 なら elem_slots = 内側次元の
 * 総スカラ数 (sub_dims の積) として計算する。 */
#include <assert.h>

/* (1) 基本形: 2D int 配列 */
struct P { int x[3][3]; };
struct P p = { .x = { [0] = {1, 2, 3}, [2] = { [2] = 9 } } };

/* (2) 3D + ネスト designator */
struct Q { int v[2][2][2]; };
struct Q q = { .v = { [1] = { [1] = { [1] = 99 } } } };

int main(void) {
  /* (1) */
  assert(p.x[0][0] == 1 && p.x[0][1] == 2 && p.x[0][2] == 3);
  assert(p.x[1][0] == 0 && p.x[1][1] == 0 && p.x[1][2] == 0);
  assert(p.x[2][0] == 0 && p.x[2][1] == 0 && p.x[2][2] == 9);

  /* (2) */
  assert(q.v[0][0][0] == 0 && q.v[0][1][1] == 0);
  assert(q.v[1][0][0] == 0 && q.v[1][0][1] == 0);
  assert(q.v[1][1][0] == 0 && q.v[1][1][1] == 99);

  return 0;
}
