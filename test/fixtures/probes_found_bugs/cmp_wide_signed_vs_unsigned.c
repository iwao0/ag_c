// 符号付き/符号なし比較の通常算術変換 (C11 6.3.1.8)。
// long(64bit) は unsigned int(32bit) の全値を表現できるため、
// `long < unsigned int` は符号付き比較となり -1 < 1 は真。
// 修正前: どちらかが unsigned なら無条件に符号なし比較し、-1 を巨大値に
//         変換して偽を返していた (ir_builder の比較符号判定が rank 無視)。
// 期待: exit=15
#include <assert.h>
int main(void) {
  long s = -1;
  unsigned int u = 1;
  int r = 0; (void)r;
  assert(s < u);   // -1 < 1   (signed) => true
  assert(u > s);   // 1  > -1  (signed) => true
  assert(s <= u);   // -1 <= 1  (signed) => true
  assert(s == -1);   // sanity
  return 0;              // 1|2|4|8 = 15
}
