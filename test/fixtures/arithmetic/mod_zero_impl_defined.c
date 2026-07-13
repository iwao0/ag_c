// 0 除算の剰余は実装定義 (ag_c は LHS をそのまま返す)
// 期待: exit=10
#include <assert.h>
int main() { assert(10%0 == 10); return 0; }
