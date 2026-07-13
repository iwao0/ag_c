// 1 を 30 bit 左シフトしても正であること (符号 bit に達していない)
// 期待: exit=1
#include <assert.h>
int main() { assert((1 << 30) > 0); return 0; }
