// 相互再帰 even / odd
// even(10) = 1、 odd(7) = 1 → 1*10+1 = 11
// 期待: exit=11
int even(int n);
int odd(int n) { return n == 0 ? 0 : even(n - 1); }
int even(int n) { return n == 0 ? 1 : odd(n - 1); }
int main(void) {
    return even(10) * 10 + odd(7);
}
