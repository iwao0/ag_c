// 乗算が加算より優先
// 期待: exit=47 (5+42)
#include <assert.h>
int main() { assert(5+6*7 == 47); return 0; }
