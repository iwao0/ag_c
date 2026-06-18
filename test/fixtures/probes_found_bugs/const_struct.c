// const struct メンバアクセス
#include <assert.h>
struct C { int v; };
int main(void) {
  const struct C c = {42};
  assert(c.v == 42);
  return 0;
}
// 期待: 42
