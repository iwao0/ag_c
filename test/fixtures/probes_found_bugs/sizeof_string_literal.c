// 文字列リテラルの sizeof (配列だから null 含む)
#include <assert.h>
int main(void) {
  assert((int)sizeof("hello") == 6); return 0;
}
// 期待: 6
