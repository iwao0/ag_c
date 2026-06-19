// sizeof(_Complex float) = 8 (実部+虚部各 4)
// 期待: exit=8
#include <assert.h>
int main(void) { assert(sizeof(_Complex float) == 8); return 0; }
