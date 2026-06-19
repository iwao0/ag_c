// long % unsigned int も同様に符号付き剰余となり -7 % 3 = -1。
// 修正前: 符号なし剰余で誤った値を返していた。
// 期待: exit=99  (-1 + 100)
#include <assert.h>
int main(void) {
  long a = -7;
  unsigned int b = 3;
  long r = a % b;   // signed modulo => -1
  assert(r == -1); assert((int)(r + 100) == 99); return 0;
}
