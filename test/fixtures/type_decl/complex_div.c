// _Complex 除算
// 期待: exit=1
int main(void) {
    _Complex double a = 10.0;
    _Complex double b = 2.0;
    _Complex double c = a / b;
    _Complex double d = 5.0;
    return c == d;
}
