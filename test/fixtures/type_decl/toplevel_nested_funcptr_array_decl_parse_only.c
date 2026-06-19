// 冗長括弧付きグローバル関数ポインタ配列 (パース確認)
// 期待: exit=0
#include <assert.h>
int (*(*arr[2]))(int);
int main(void) { return 0; }
