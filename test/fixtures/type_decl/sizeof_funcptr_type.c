// sizeof(関数ポインタ) = 8
// 期待: exit=8
#include <assert.h>
int main(void) { assert(sizeof(int (*)(int)) == 8); return 0; }
