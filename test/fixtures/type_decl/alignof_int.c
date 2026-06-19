// _Alignof(int) = 4
// 期待: exit=4
#include <assert.h>
int main(void) { assert(_Alignof(int) == 4); return 0; }
