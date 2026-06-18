// 配列サイズに定数式を使えること
// `int arr[1+2]` で要素数 3 として扱われる。
// 期待: exit=3
#include <assert.h>
int main(void) {
    int arr[1 + 2];
    arr[0] = 1;
    arr[1] = 2;
    arr[2] = 3;
    assert(sizeof(arr) == 12);
    assert(arr[0] == 1);
    assert(arr[1] == 2);
    assert(arr[2] == 3);
    return 0;
}
