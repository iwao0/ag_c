// sizeof(int * volatile) = 8
// 期待: exit=8
#include <assert.h>
int main(void) { assert(sizeof(int * volatile) == sizeof(void*)); return 0; }
