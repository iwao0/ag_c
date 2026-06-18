// 同じ幅の int vs unsigned int は符号なし比較のまま (両辺を unsigned int に
// 変換)。(int)-1 < (unsigned int)1 は -1 が巨大値となり偽。
// 広い符号付き型を符号付き比較に直した修正が、同幅ケースまで符号付きに
// しないことを保証する回帰テスト。
// 期待: exit=62
#include <assert.h>
int main(void) {
  int s = -1;
  unsigned int u = 1;
  // 同幅 int vs unsigned は符号なし比較: -1 は巨大値になる
  assert(s >= u);    // huge >= 1 => true
  assert(!(s < u));  // huge < 1 => false (符号付き比較に直してはいけない回帰確認)
  return 0;
}
