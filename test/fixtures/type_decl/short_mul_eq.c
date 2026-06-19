// short *= 3
// 期待: exit=30
#include <assert.h>
int main(void) { short s = 10; s *= 3; assert(s == 30); return 0; }
