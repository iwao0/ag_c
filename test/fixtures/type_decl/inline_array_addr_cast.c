// inline 多次元配列の `&arr` を (int*) キャストして使う。
// 期待: exit=0
#include <assert.h>
int main(void) {
    int arr[2][3][4];
    arr[0][0][0] = 99;
    int *p = (int*)&arr;
    assert(p[0] == 99);
    return 0;
}
