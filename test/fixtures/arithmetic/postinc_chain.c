// `x+++y+++z` は `(x++) + (y++) + z` と読まれる
// x++=2, y++=3, z=4 → 2+3+4=9
// 期待: exit=9
#include <assert.h>
int main(void) { int x=2; int y=3; int z=4; assert(x+++y+++z == 9); return 0; }
