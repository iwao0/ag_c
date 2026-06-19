// f サフィックスの 16 進 float リテラル 0x1.8p+3f = 12.0
// 期待: exit=0
#include <assert.h>
int main(void) {
    float f = 0x1.8p+3f;
    float diff = f - 12.0f;
    if (diff < 0.0f) diff = -diff;
    assert(diff < 1e-4f);
    return 0;
}
