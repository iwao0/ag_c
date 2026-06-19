// float.h の FLT_MAX
// 期待: exit=42
#include <float.h>
#include <assert.h>
int main(void) {
    float f = FLT_MAX;
    assert(f > 1.0F);
    return 0;
}
