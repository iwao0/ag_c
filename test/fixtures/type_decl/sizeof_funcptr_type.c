// sizeof(関数ポインタ) = target pointer size
#include <assert.h>
int main(void) { assert(sizeof(int (*)(int)) == sizeof(void*)); return 0; }
