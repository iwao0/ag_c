/* クロス TU 内部リンケージ回帰 — もう一方の TU (main を含まない)。
 * main 側 TU と同名の file-scope static 変数 s / static 関数 base を持つ。
 * 内部リンケージが正しければ main 側の s/base とは別シンボルになる。 */
static int s = 5;
static int base(void) { return s; }

int other_val(void) { return base() + 1; }  /* 5 + 1 = 6 */
