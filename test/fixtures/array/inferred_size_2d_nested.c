// 2D 配列の外側サイズ推定 (ネスト初期化子)
// 修正前: 推定タイミングが trailing `[3]` 解析前で `=` に到達できず
//        "配列サイズは正の整数である必要があります" エラー
// 期待: exit=7 (a[0][0]=1, a[1][2]=6)
#include <assert.h>
int main(void) {
    int a[][3] = { {1,2,3}, {4,5,6} };
    assert(a[0][0] == 1);
    assert(a[0][1] == 2);
    assert(a[0][2] == 3);
    assert(a[1][0] == 4);
    assert(a[1][1] == 5);
    assert(a[1][2] == 6);
    return 0;
}
