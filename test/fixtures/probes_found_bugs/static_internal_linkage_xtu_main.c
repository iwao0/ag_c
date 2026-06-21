/* クロス TU 内部リンケージ回帰 — main 側 TU。
 * もう一方の TU (static_internal_linkage_xtu_other.c) と同名の file-scope
 * static 変数 s / static 関数 base を意図的に持つ。内部リンケージ (C11 6.2.2p3)
 * が守られていれば、両 TU をリンクしても各 TU の s/base は独立し衝突しない。
 * バグで static が .global になると、E2E ハーネスの記号名前空間化後に同名 .global
 * が両 TU で重複し、category binary のリンクが duplicate symbol で失敗する (= 回帰検出)。
 * main は 42 を返す。 */
extern int other_val(void);

static int s = 30;
static int base(void) { return s; }

int main(void) { return base() + other_val() + 6; }  /* 30 + 6 + 6 = 42 */
