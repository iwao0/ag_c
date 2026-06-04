// グローバル関数ポインタ配列に代入してから呼ぶ
// ops[0](-5) = neg(-5) = 5、 ops[1](10) = dbl(10) = 20、合計 = 25
// 期待: exit=25
int (*ops[2])(int);
int neg(int x) { return -x; }
int dbl(int x) { return x * 2; }
int main(void) {
    ops[0] = neg;
    ops[1] = dbl;
    return ops[0](-5) + ops[1](10);
}
