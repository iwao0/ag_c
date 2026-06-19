// 文字列ポインタ配列
#include <assert.h>
const char *names[] = {"alpha", "beta", "gamma"};
int len(const char *s) { int n = 0; while (*s++) n++; return n; }
int main(void) {
  assert(len(names[0]) == 5); assert(len(names[1]) == 4); assert(len(names[2]) == 5); return 0;  // 5+4+5 = 14
}
// 期待: 14
