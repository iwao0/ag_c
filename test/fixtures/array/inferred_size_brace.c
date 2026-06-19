// 配列サイズ推定: int a[] = {...}
// 修正前: E3064 "数値が必要です ']'" でコンパイル失敗
// 期待: exit=100
#include <assert.h>
int main(void) {
    int a[] = {10, 20, 30, 40};
    assert(a[0] == 10);
    assert(a[1] == 20);
    assert(a[2] == 30);
    assert(a[3] == 40);
    return 0;
}
