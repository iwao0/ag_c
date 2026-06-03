// `x+++-y` は `(x++) + (-y)` と読まれる
// x++ 評価値 1、 -y=-2 → -1 (mod 256 = 255)
// 期待: exit=255
int main(void) { int x=1; int y=2; return x+++-y; }
