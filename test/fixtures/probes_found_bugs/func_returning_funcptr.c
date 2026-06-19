// 関数戻り値型として関数ポインタ
#include <assert.h>
int sq(int x) { return x * x; }
int cube(int x) { return x * x * x; }
int (*choose(int n))(int) { return n == 2 ? sq : cube; }
int main(void) {
  int (*op)(int) = choose(2);
  assert(op(5) == 25); return 0;  // 25
}
// 期待: 25
