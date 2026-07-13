// > (同値は偽)
// 期待: exit=0
#include <assert.h>
int main() { assert(!(1>1)); return 0; }
