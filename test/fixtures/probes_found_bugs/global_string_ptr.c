// グローバル char *p = "...";
#include <assert.h>
char *greet = "hello world";
int main(void) {
  int n = 0;
  while (greet[n]) n++;
  assert(n == 11); return 0;  // 11
}
// 期待: 11
