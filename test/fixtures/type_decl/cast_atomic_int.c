// (_Atomic(int))42 (受理確認)
// 期待: exit=42
#include <assert.h>
int main(void) { assert((_Atomic(int))42 == 42); return 0; }
