// 単項 - + 後置インクリメント
// -a++ → -(a++評価値=1) = -1、 -1 → -2 (mod 256 = 254)
// 期待: exit=254
#include <assert.h>
int main(void) { int a=1; assert(-a++-1 == -2); return 0; }
