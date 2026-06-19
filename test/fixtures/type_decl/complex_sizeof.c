// sizeof(_Complex double) = 16 (実部+虚部各 8)
// 期待: exit=16
#include <assert.h>
int main(void) { assert(sizeof(_Complex double) == 16); return 0; }
