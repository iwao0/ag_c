// sizeof(int (*)[3]) = 8 (ポインタ)
// 期待: exit=8
#include <assert.h>
int main(void) { assert(sizeof(int (*)[3]) == 8); return 0; }
