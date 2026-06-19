// 三項の連鎖 (どちらも偽 → 最後の値)
// 0 ? 1 : (0 ? 2 : 3) = 3
// 期待: exit=3
#include <assert.h>
int main(void) { assert((0 ? 1 : 0 ? 2 : 3) == 3); return 0; }
