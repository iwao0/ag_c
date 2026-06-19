// グローバル int* を &g で初期化
// 期待: exit=0
#include <assert.h>
int g = 99;
int *gp = &g;
int main(void) {
    assert(*gp == 99);
    return 0;
}
