// グローバル struct with func ptr
#include <assert.h>
int sq(int x) { return x * x; }
struct Op { int (*f)(int); };
struct Op gop = {sq};
int main(void) {
  assert(gop.f(7) == 49); return 0;  // 49
}
// 期待: 49
