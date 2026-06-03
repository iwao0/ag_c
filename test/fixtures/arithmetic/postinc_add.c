// `x+++y` は `x++ + y` と読まれる
// x++ で x=2 評価値 1、 +y=2 → 1+2=3
// 期待: exit=3
int main(void) { int x=1; int y=2; return x+++y; }
