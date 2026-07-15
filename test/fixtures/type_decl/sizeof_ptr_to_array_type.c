// sizeof(int (*)[3]) = target pointer size
#include <assert.h>
int main(void) { assert(sizeof(int (*)[3]) == sizeof(void*)); return 0; }
