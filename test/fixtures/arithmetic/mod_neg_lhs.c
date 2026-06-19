// 負数 % 正数 の挙動 (truncation toward zero)
// -10 % 3 == -1
// 期待: exit=1
#include <assert.h>
main() { assert((-10%3) == -1); return 0; }
