// _Complex 乗算
// 期待: exit=1
int main(void) {
    _Complex double a = 3.0;
    _Complex double b = 4.0;
    _Complex double c = a * b;
    _Complex double d = 12.0;
    return c == d;
}
