// float 除算 (F サフィックス)
// 期待: exit=0
#include <assert.h>
int main(void) {
    float f = 10.5F; float g = 3.0F; float r = f / g;
    float diff = r - 3.5f;
    if (diff < 0.0f) diff = -diff;
    assert(diff < 1e-4f);
    return 0;
}
