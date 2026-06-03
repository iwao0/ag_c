// 後置インクリメント + 乗算
// x++ 評価値 2、 * y=3 → 6
// 期待: exit=6
int main(void) { int x=2; int y=3; return x++*y; }
