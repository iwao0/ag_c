// signed char に 200 を代入 → -56 として解釈
// 期待: exit=1
#include <assert.h>
int main(void) {
    signed char c = 200;
    assert((c < 0) == 1);
    return 0;
}
