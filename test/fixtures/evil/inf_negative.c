// 負の無限大は小さな負値より小さい
// 期待: exit=1
int main(void) {
    double x = -1.0 / 0.0;
    return x < -1000000.0;
}
