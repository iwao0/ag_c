// 文字列を関数引数で渡す
#include <assert.h>
int len(const char *s) { int n=0; while (*s++) n++; return n; }
int main(void) {
  assert(len("foobar") == 6);
  return 0;
}
// 期待: 6
