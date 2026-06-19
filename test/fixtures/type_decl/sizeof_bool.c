// sizeof(_Bool) = 1
// 期待: exit=1
#include <assert.h>
int main(void) { assert(sizeof(_Bool) == 1); return 0; }
