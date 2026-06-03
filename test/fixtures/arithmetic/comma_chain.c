// カンマ演算子の連鎖
// (a=1, b=2, a+b) → 3
// 期待: exit=3
main() { a=0; b=0; return (a=1, b=2, a+b); }
