/* グローバル struct ポインタ配列の subscript + メンバアクセス + post-inc:
 *   struct P { int x, y; };
 *   struct P pts[3] = {{1,2},{3,4},{5,6}};
 *   struct P *parr[3] = {&pts[0], &pts[1], &pts[2]};
 *   parr[i]->x;           /* 旧: E3005 (-> 左辺が struct ポインタじゃない) *\/
 *   parr[i++]->x;         /* 同上 *\/
 *
 * 以前は try_build_global_var_node の配列 decay 経路 (ND_ADDR ラップ) で
 * pointer_qual_levels / base_deref_size を立てない (= struct タグ配列は分岐に乗らず
 * 関数ポインタ配列分岐は tag_kind==TK_EOF ガード) ため、subscript 結果が「要素はポインタ」
 * 分岐に乗らず、tag は伝播しても is_tag_pointer=0 のままで `->` が拒否されていた。
 * メンバ struct ポインタ配列 (db98d34) は対応済みだったがグローバルは漏れていた。
 * さらに emit_global_struct_array_init が struct 配列展開 (各要素をメンバ単位に出す)
 * を呼び、ポインタ要素 (8B) を struct (12B) として出力 → unaligned リンクエラー。
 *
 * 現在は canonical type の array(pointer(struct P)) を subscript ごとに辿り、
 * `parr[i]` の結果を pointer(struct P) として `->` 解決する。
 * (arch/arm64_apple/arm64_apple.c) emit_one_global_var の struct 配列分岐に `!gv->is_tag_pointer`
 *   ガードを追加。struct ポインタ配列は scalar emit 経路に流して 8B ポインタとして出力する。 */
#include <assert.h>

struct P { int x, y; };
struct P pts[3] = {{1, 2}, {3, 4}, {5, 6}};
struct P *parr[3] = {&pts[0], &pts[1], &pts[2]};

int main(void) {
  /* (1) 基本: subscript + -> (全要素確認、続き25 で init slot 計算修正済み) */
  assert(parr[0]->x == 1 && parr[0]->y == 2);
  assert(parr[1]->x == 3 && parr[1]->y == 4);
  assert(parr[2]->x == 5 && parr[2]->y == 6);

  /* (2) ローカル struct ポインタ配列 (回帰確認) */
  struct P lpts = {7, 8};
  struct P *larr[3] = {&lpts, &lpts, &lpts};
  int i = 0;
  int v = larr[i++]->x;
  assert(v == 7 && i == 1);
  v = larr[i++]->y;
  assert(v == 8 && i == 2);

  /* (3) グローバル post-inc 連鎖 */
  int j = 0;
  int w = parr[j++]->x;
  assert(w == 1 && j == 1);
  w = parr[j++]->y;
  assert(w == 4 && j == 2);

  return 0;
}
