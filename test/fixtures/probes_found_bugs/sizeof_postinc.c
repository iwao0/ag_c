// sizeof(int_expr++) は 4 (n++ の型は int rvalue)
#include <assert.h>
int main(void) {
  int n = 0;
  assert((int)sizeof(n++) == 4); return 0;
}
// 期待: 4
