// _Alignof(int * volatile) = 8
// 期待: exit=8
#include <assert.h>
int main(void) { assert(_Alignof(int * volatile) == _Alignof(void*)); return 0; }
