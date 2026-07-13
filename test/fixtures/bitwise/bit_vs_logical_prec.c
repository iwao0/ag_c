// `|` > `&&` の優先順位確認
// 1 && (2 | 0) = 1 && 2 = 1
// 期待: exit=1
#include <assert.h>
int main() { assert((1 && 2 | 0) == 1); return 0; }
