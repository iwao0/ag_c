// char += 1 の挙動
// 期待: exit=3
#include <assert.h>
int main(void) { char c = 1; c += 2; assert(c == 3); return 0; }
