// 後置デクリメント
// a-- 後 a=2、 b=旧値=3、 return 2*10+3 = 23
// 期待: exit=23
#include <assert.h>
int main(void) { int a = 3; int b = a--; assert(a * 10 + b == 23); return 0; }
