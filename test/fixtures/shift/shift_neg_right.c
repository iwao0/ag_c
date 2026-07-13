// 負数の右シフトは算術シフト (arithmetic shift right)
// -8 >> 1 == -4 を確認
// 期待: exit=1
#include <assert.h>
int main() { assert((-8 >> 1) == -4); return 0; }
