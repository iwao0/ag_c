// `x---y` は `x-- - y` と読まれる
// x-- 評価値 3、 -y=2 → 3-2=1
// 期待: exit=1
int main(void) { int x=3; int y=2; return x---y; }
