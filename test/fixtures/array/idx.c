// 配列の基本: 添字での書き込みと読み出し
// arr[0]=1, arr[1]=2, arr[2]=3 と代入してから arr[2] を返す。
// 期待: exit=3
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
