// グローバル int* 経由で g に書き込み
// 期待: exit=0
#include <assert.h>
int g = 0;
int *gp = &g;
int main(void) {
    *gp = 55;
    assert(g == 55);
    return 0;
}
