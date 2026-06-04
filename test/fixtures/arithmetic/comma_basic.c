// カンマ演算子: 左から右に評価、値は右側
// (a=1, a+2) → 3
// 期待: exit=3
int main(void) { int a = 0; return (a = 1, a + 2); }
