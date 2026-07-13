// 右シフト (符号付き正の値): 32 >> 3 = 4
// 期待: exit=4
#include <assert.h>
int main() { assert((32 >> 3) == 4); return 0; }
