// 別関数の static 変数は独立 (名前空間が分離) する
// f は 1→2、g は 100→101 と進む
// 期待: exit=104 ((1+2) + (100+101) = 204、204 mod 256 = 204... or actually
//        1 + 2 + 100 + 101 = 204 だがコード冒頭 g 2 回呼んでない: 1+100+1=102? 再考)
// 実際: f() = 1, g() = 100, f() = 2 (前回 1 を覚えている), g() = 101
//   sum = 1 + 100 + 2 + 101 = 204
// 期待: exit=204
int f(void) { static int n = 0; return ++n; }
int g(void) { static int n = 99; return ++n; }
int main(void) {
    return f() + g() + f() + g();
}
