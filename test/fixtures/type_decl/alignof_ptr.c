// _Alignof(int*) = target pointer alignment
#include <assert.h>
int main(void) { assert(_Alignof(int*) == _Alignof(void*)); return 0; }
