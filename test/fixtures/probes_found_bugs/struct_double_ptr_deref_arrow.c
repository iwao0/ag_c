/* `struct P **pp = &arr[i]; (*pp)->x` の `*pp` から `->` 解決:
 *   struct P { int x, y; };
 *   struct P *parr[3] = {...};
 *   struct P **pp = &parr[1];
 *   (*pp)->x  /* 旧: E3005 (`->` 左辺が struct ポインタじゃない) *\/
 *
 * 以前は build_unary_deref_node が pql>=2 の中間 deref で tag info / is_tag_pointer の
 * carry が不完全で `*pp` の結果が struct ポインタとして認識されず `->` 経由のメンバ
 * アクセスが E3005 で拒否されていた。
 *
 * 続き25 (try_build_global_var_node で tag-pointer 配列に pql=1/bds=struct を立てる) と
 * 続き26 (gbrace_child_at で tag-pointer 配列を scalar pointer 化) の修正で副次的に
 * 解決した: parr (ND_ADDR decay) が pql=1/bds=12 を持ち、`&parr[1]` で得た pp の型情報が
 * struct P** として正しく carry されるため、`*pp` の build_unary_deref_node 経路が
 * 中間 ND_DEREF (pql>=2) として struct タグ情報を引き継ぎ、`->` 解決に乗る。
 *
 * 本 fixture はローカル/グローバル両方の `(*pp)->m` パターンを網羅して回帰防止。 */
#include <assert.h>

struct P { int x, y; };

struct P pts_g = {10, 20};
struct P *parr_g[3] = {&pts_g, &pts_g, &pts_g};

int main(void) {
  /* (1) ローカル struct P **pp = &arr[i] → (*pp)->m */
  struct P pts_l = {3, 4};
  struct P *parr_l[3] = {&pts_l, &pts_l, &pts_l};
  struct P **pp_l = &parr_l[1];
  assert((*pp_l)->x == 3 && (*pp_l)->y == 4);

  /* (2) ポインタ算術後の deref + -> (`*(pp + n)->m`) */
  struct P **pp = parr_l;
  assert((*(pp + 2))->x == 3);

  /* (3) グローバル ptr 配列の &parr_g[i] 経由 */
  struct P **pp_g = &parr_g[1];
  assert((*pp_g)->x == 10 && (*pp_g)->y == 20);
  assert((*(pp_g + 0))->x == 10);

  return 0;
}
