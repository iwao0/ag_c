// 指定初期化子 [N]= による要素割り当て (C99)
// arr[0]=1, arr[2]=7, 他は 0
// 期待: exit=8 (arr[0]+arr[2])
#include <assert.h>
int main(void) {
    int arr[4] = { [2] = 7, [0] = 1 };
    assert(arr[0] == 1);
    assert(arr[1] == 0);
    assert(arr[2] == 7);
    assert(arr[3] == 0);
    return 0;
}
