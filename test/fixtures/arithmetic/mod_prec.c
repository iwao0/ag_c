// %% の優先順位 (* / と同じ、+ より高い)
// 10 + (7%4)*2 = 10 + 3*2 = 16
// 期待: exit=16
#include <assert.h>
int main() { assert(10+7%4*2 == 16); return 0; }
