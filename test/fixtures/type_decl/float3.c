// float 減算
// 期待: exit=0
#include <assert.h>
int main(void) {
    float f = 5.5; float g = 3.2; float r = f - g;
    float diff = r - 2.3f;
    if (diff < 0.0f) diff = -diff;
    assert(diff < 1e-3f);
    return 0;
}
