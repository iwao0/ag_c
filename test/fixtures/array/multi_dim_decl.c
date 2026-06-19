// 多次元配列宣言が単独で受理されること (初期化や読み書きはせず宣言のみ)
// 期待: exit=7
#include <assert.h>
int main(void) {
    int arr[2][3];
    (void)arr;
    return 0;
}
