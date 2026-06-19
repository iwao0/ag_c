// sizeof((int[3])) = 12
// 期待: exit=12
#include <assert.h>
int main(void) { assert(sizeof((int[3])) == 12); return 0; }
