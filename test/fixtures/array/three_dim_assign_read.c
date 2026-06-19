// 3D 配列の代入と読み出し (初期化子なし)
// a[1][1][2] に 12 を書き、同じ位置を読む。
// 期待: exit=12
#include <assert.h>
int main(void) {
    int a[2][2][3];
    a[1][1][2] = 12;
    assert(a[1][1][2] == 12);
    return 0;
}
