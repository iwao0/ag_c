// 配列を for ループで書き込み・読み出し
// arr[i] = i+1 → 1+2+...+10 = 55
// 期待: exit=55
#include <assert.h>
int main(void) {
    int arr[10];
    int i;
    for (i = 0; i < 10; i = i + 1) arr[i] = i + 1;
    assert(arr[0] == 1);
    assert(arr[1] == 2);
    assert(arr[2] == 3);
    assert(arr[3] == 4);
    assert(arr[4] == 5);
    assert(arr[5] == 6);
    assert(arr[6] == 7);
    assert(arr[7] == 8);
    assert(arr[8] == 9);
    assert(arr[9] == 10);
    int sum = 0;
    for (i = 0; i < 10; i = i + 1) sum = sum + arr[i];
    assert(sum == 55);
    return 0;
}
