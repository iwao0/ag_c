// float 加算
// 期待: exit=0
#include <assert.h>
int main(void) {
    float f = 3.14; float g = 4.2; float r = f + g;
    float diff = r - 7.34f;
    if (diff < 0.0f) diff = -diff;
    assert(diff < 1e-3f);
    return 0;
}
