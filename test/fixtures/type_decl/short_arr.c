// short 配列の添字
// 期待: exit=30
#include <assert.h>
int main(void) {
    short arr[3];
    arr[0] = 10; arr[1] = 20; arr[2] = 30;
    assert(arr[2] == 30);
    return 0;
}
