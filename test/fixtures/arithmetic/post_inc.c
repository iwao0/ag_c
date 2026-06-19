// 後置インクリメント: 評価後に増える
// a++ 後 a=2、 b=旧値=1、 return 2*10+1 = 21
// 期待: exit=21
#include <assert.h>
int main(void) { int a = 1; int b = a++; assert(a * 10 + b == 21); return 0; }
