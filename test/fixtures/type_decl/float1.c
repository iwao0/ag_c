// float 初期化と return
// 期待: exit=0
#include <assert.h>
int main(void) {
    float f = 7;
    float diff = f - 7.0f;
    if (diff < 0.0f) diff = -diff;
    assert(diff < 1e-4f);
    return 0;
}
