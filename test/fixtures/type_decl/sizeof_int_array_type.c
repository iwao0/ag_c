// sizeof(int[10]) = 40
// 期待: exit=40
#include <assert.h>
int main(void) { assert(sizeof(int[10]) == 40); return 0; }
