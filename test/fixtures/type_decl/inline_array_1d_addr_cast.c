// 1 次元配列に対する `&arr` のキャスト経由アクセス。
// arr[2] = 33 を書いて (int*)&arr で [2] を読み戻す。
// 期待: exit=0
#include <assert.h>
int main(void) {
    int arr[5];
    arr[2] = 33;
    int *p = (int*)&arr;
    assert(p[2] == 33);
    return 0;
}
