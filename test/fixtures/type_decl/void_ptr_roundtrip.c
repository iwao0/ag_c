// void* を介した int* のラウンドトリップ
// 期待: exit=5
#include <assert.h>
int main(void) {
    int x = 5;
    void *v = &x;
    int *p = (int*)v;
    assert(*p == 5);
    return 0;
}
