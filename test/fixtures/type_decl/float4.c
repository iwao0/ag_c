// float 乗算 (f サフィックス)
// 期待: exit=0
#include <assert.h>
int main(void) {
    float f = 6.0f; float g = 2.5f; float r = f * g;
    float diff = r - 15.0f;
    if (diff < 0.0f) diff = -diff;
    assert(diff < 1e-4f);
    return 0;
}
