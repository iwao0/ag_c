// ビット反転: ~5 = -6 (mod 256 = 250)
// 期待: exit=250
#include <assert.h>
main() { assert(~5 == -6); return 0; }
