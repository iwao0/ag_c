// _Alignof(int*) = 8
// 期待: exit=8
#include <assert.h>
int main(void) { assert(_Alignof(int*) == 8); return 0; }
