// long / unsigned int は通常算術変換 (C11 6.3.1.8) で long(signed) に揃うため
// 符号付き除算となり -7 / 3 = -2。
// 修正前: どちらかが unsigned なら符号なし除算し、-7 を巨大値に変換して誤った
//         商を返していた (ir_builder の DIV 符号判定が rank 無視)。
// 期待: exit=48  (-2 + 50)
#include <assert.h>
int main(void) {
  long a = -7;
  unsigned int b = 3;
  long q = a / b;   // signed division => -2
  assert(q == -2); assert((int)(q + 50) == 48); return 0;
}
