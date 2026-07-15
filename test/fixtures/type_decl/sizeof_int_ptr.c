// sizeof(int*) = target pointer size
#include <assert.h>
int main(void) { assert(sizeof(int*) == sizeof(void*)); return 0; }
