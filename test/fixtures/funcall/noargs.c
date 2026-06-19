// 引数なし関数の呼び出し
// 期待: exit=42
#include <assert.h>
fortytwo() { return 42; }
main() { assert(fortytwo() == 42); return 0; }
