// 三項演算子のネスト (右結合: 0 ? 1 : (1 ? 2 : 3))
// 期待: exit=2
#include <assert.h>
main() { assert((0 ? 1 : 1 ? 2 : 3) == 2); return 0; }
