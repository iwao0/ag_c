// sizeof(typedef 配列型へのポインタ) = 8
// 期待: exit=8
#include <assert.h>
typedef int A3[3];
int main(void) { assert(sizeof(A3 (*)[2]) == 8); return 0; }
