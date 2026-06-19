// エスケープシーケンス
#include <assert.h>
int main(void) {
  char s[] = "\t\n\\";
  assert(s[0] == 9); assert(s[1] == 10); assert(s[2] == 92); return 0;  // 9 + 10 + 92 = 111
}
// 期待: 111
