// VLA (可変長配列) の基本: 実行時サイズで宣言 → 要素読み書き
// 期待: exit=42 (10+20+12)
#include <assert.h>
int main(void) {
    int n = 3;
    int a[n];
    a[0] = 10;
    a[1] = 20;
    a[2] = 12;
    assert(a[0] == 10);
    assert(a[1] == 20);
    assert(a[2] == 12);
    return 0;
}
