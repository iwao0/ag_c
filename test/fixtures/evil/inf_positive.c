// 正の無限大は大きな値より大きい
// 期待: exit=1
int main(void) {
    double x = 1.0 / 0.0;
    return x > 1000000.0;
}
