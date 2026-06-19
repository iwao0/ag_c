// extern int g + inline 関数の受理確認
// 期待: exit=0
#include <assert.h>
extern int g;
inline int add(int a, int b) { return a + b; }
int main(void) { assert(add(3, 4) == 7); return 0; }
