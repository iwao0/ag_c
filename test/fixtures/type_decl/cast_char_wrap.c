// (char)300 → mod 256 = 44
// 期待: exit=44
#include <assert.h>
int main(void) { assert((char)300 == 44); return 0; }
