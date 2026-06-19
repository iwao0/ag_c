// sizeof(arr) / sizeof(arr[0]) で要素数を取得する慣用句
// int a[10] → sizeof(a) = 40、sizeof(a[0]) = 4 → 10
// 期待: exit=10
#include <assert.h>
int main(void) {
    int a[10];
    assert((int)(sizeof(a) / sizeof(a[0])) == 10);
    return 0;
}
