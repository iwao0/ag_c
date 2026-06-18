// 配列の波括弧初期化: int arr[3] = {1, 2, 3};
// 期待: exit=3 (arr[2])
#include <assert.h>
int main(void) {
    int arr[3] = {1, 2, 3};
    assert(arr[0] == 1);
    assert(arr[1] == 2);
    assert(arr[2] == 3);
    return 0;
}
