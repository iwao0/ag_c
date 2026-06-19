// sizeof(x) (式版) = 4
// 期待: exit=4
#include <assert.h>
int main(void) { int x = 3; assert(sizeof(x) == 4); return 0; }
