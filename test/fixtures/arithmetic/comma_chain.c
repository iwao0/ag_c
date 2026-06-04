// カンマ演算子の連鎖
// (a=1, b=2, a+b) → 3
// 期待: exit=3
int main(void) { int a = 0; int b = 0; return (a = 1, b = 2, a + b); }
