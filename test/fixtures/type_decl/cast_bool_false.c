// (_Bool)0 → 0
// 期待: exit=0
#include <assert.h>
int main(void) { assert((_Bool)0 == 0); return 0; }
