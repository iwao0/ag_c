// 同じ幅の int vs unsigned int は符号なし比較のまま (両辺を unsigned int に
// 変換)。(int)-1 < (unsigned int)1 は -1 が巨大値となり偽。
// 広い符号付き型を符号付き比較に直した修正が、同幅ケースまで符号付きに
// しないことを保証する回帰テスト。
// 期待: exit=62
int main(void) {
  int s = -1;
  unsigned int u = 1;
  int r = 0;
  if (s < u)  r |= 1;   // (unsigned) huge < 1 => false
  if (s >= u) r |= 2;   // (unsigned) huge >= 1 => true
  return r + 60;        // 2 + 60 = 62
}
