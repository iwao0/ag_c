// sizeof(long double) = 8 (ag_c 実装)
// 期待: exit=8
#include <assert.h>
int main(void) { assert(sizeof(long double) == 8); return 0; }
