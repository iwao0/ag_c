// +Inf + -Inf = NaN
// 期待: exit=1
int main(void) {
    double a = 1.0 / 0.0;
    double b = -1.0 / 0.0;
    double c = a + b;
    return c != c;
}
