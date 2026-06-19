// プロトタイプ宣言 + 定義 + 呼び出し
// 期待: exit=42 (20+22)
#include <assert.h>
int add(int a, int b);
int add(int a, int b) { return a + b; }
int main(void) {
    assert(add(20, 22) == 42);
    return 0;
}
