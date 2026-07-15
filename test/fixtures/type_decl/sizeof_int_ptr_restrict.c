// sizeof(int * restrict) = 8
// 期待: exit=8
#include <assert.h>
int main(void) { assert(sizeof(int * restrict) == sizeof(void*)); return 0; }
