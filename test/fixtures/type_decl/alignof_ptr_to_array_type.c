// _Alignof(int (*)[3]) = 8
// 期待: exit=8
#include <assert.h>
int main(void) { assert(_Alignof(int (*)[3]) == _Alignof(void*)); return 0; }
