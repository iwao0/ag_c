// 再帰関数 (階乗)
// 5! = 120
// 期待: exit=120
fact(n) { if (n <= 1) return 1; return n * fact(n - 1); }
main() { return fact(5); }
