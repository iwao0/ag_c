// NaN == NaN は偽
// 期待: exit=1
int main(void) {
    double x = 0.0 / 0.0;
    return !(x == x);
}
