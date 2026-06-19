// 2 次元配列に対する `&arr` のキャスト経由アクセス。
// arr[2][3] = 77、((int*)&arr)[11] で読み戻す。
// 期待: exit=0
#include <assert.h>
int main(void) {
    int arr[3][4];
    arr[2][3] = 77;
    int *p = (int*)&arr;
    assert(p[2 * 4 + 3] == 77);
    return 0;
}
