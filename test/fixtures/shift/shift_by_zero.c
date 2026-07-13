// 0 シフトは値が変わらないこと
// 期待: exit=1
#include <assert.h>
int main() { assert((5 << 0) == 5); return 0; }
