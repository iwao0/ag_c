// float.h の FLT_RADIX は 2 (IEEE754 binary)
// 期待: exit=42
#include <float.h>
#include <assert.h>
int main(void) {
    assert(FLT_RADIX == 2);
    return 0;
}
