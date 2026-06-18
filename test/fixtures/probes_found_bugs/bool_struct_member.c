// _Bool メンバ
#include <assert.h>
struct S { int a; _Bool b; int c; };
int main(void) {
  struct S s;
  s.a = 10;
  s.b = 42;  // 1 に正規化
  s.c = 30;
  assert(s.a + s.b + s.c == 41);  // 41
  return 0;
}
// 期待: 41
