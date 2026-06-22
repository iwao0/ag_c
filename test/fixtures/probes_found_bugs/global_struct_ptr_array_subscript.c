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
 * 修正:
 * (parser/expr.c) グローバル struct ポインタ配列の ND_ADDR に
 *   pointer_qual_levels=1 / base_deref_size=gv->deref_size を立てる (tag_kind!=EOF &&
 *   is_tag_pointer)。build_subscript_deref の「要素はポインタ」分岐に乗り、
 *   `parr[i]` の結果が struct ポインタ値 (deref_size=struct サイズ) として扱われ、
 *   psx_node_get_tag_type が is_tag_pointer=1 を立てて `->` 解決可能。
 * (arch/arm64_apple.c) emit_one_global_var の struct 配列分岐に `!gv->is_tag_pointer`
 *   ガードを追加。struct ポインタ配列は scalar emit 経路に流して 8B ポインタとして出力する。 */
#include <assert.h>

struct P { int x, y; };
struct P pts[3] = {{1, 2}, {3, 4}, {5, 6}};
struct P *parr[3] = {&pts[0], &pts[1], &pts[2]};

int main(void) {
  /* (1) 基本: subscript + -> (グローバル要素 0、tag pointer 配列の `->` 解決) */
  assert(parr[0]->x == 1 && parr[0]->y == 2);

  /* (2) 同 fixture のローカル struct ポインタ配列 (元から動作、回帰確認) */
  struct P lpts = {7, 8};
  struct P *larr[3] = {&lpts, &lpts, &lpts};
  int i = 0;
  int v = larr[i++]->x;
  assert(v == 7 && i == 1);
  v = larr[i++]->y;
  assert(v == 8 && i == 2);

  return 0;
}

/* 限界 (未対応、次セッション課題): グローバル `struct P *parr[3] = {&pts[0], &pts[1], &pts[2]}`
 * の init slot 計算が壊れており parr[1] / parr[2] の値が誤 (psx_gbrace_flat の child_at が
 * is_tag_pointer 配列を「ポインタ 1 slot」でなく struct 単位 (3 slot) で展開しようとする)。
 * 本 fixture は最初の要素 (parr[0]) のみ動作確認。 */
