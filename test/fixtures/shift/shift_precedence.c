// シフト演算の優先順位 ( + より低い )
// 1 + 2 << 3 = (1+2) << 3 = 24
// 期待: exit=24
#include <assert.h>
main() { assert((1 + 2 << 3) == 24); return 0; }
