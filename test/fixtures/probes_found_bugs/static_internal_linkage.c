// ファイルスコープ static (内部リンケージ, C11 6.2.2p3) の変数・関数の検証。
// 回帰対象バグ: ag_c が static シンボルにも .global を出し、暫定定義を .comm
// (= common/外部シンボル) にしていたため、同名 static を持つ別 TU とリンク衝突
// (duplicate symbol) / 共有していた。修正: codegen で static は .global を出さず、
// 無初期化 static を .comm でなくローカルな .zerofill (__bss) に出す。
// クロス TU 衝突そのものは単一ファイルでは再現しないが (差分プローブで別途確認)、
// static 変数の値・無初期化ゼロ・配列・static 関数呼び出しが codegen 変更後も
// 正しいことを保証する。
#include <assert.h>

static int s_init = 5;        // 初期化あり: __data にローカルラベルで出力
static int s_noinit;          // 無初期化: .zerofill __bss (ゼロ初期化)
static int s_arr[3] = {7, 8, 9};

static int add_base(int x) { return x + s_init; }

// 関数内 static (内部リンケージ。グローバルに lowering される) の永続性。
static int tick(void) { static int n; return ++n; }

int main(void) {
  assert(s_init == 5);
  assert(s_noinit == 0);
  s_noinit = 37;
  assert(s_noinit == 37);
  assert(add_base(10) == 15);
  assert(s_arr[0] + s_arr[1] + s_arr[2] == 24);
  assert(tick() == 1);
  assert(tick() == 2);
  assert(tick() == 3);
  return 0;
}
