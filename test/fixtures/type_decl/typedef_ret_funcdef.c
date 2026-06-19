// typedef で long を別名化し、戻り値型に使う
// 期待: exit=7
#include <assert.h>
typedef long mylong;
mylong add(mylong a, mylong b) { return a + b; }
int main(void) { assert((int)add(3, 4) == 7); return 0; }
