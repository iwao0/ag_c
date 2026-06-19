// 配列要素の合計
// arr[0..2] を 1,2,3 にして合計する。
// 期待: exit=6
#include <assert.h>
int main(void) {
    int arr[3];
    arr[0] = 1;
    arr[1] = 2;
    arr[2] = 3;
    assert(arr[0] == 1);
    assert(arr[1] == 2);
    assert(arr[2] == 3);
    return 0;
}
