// _Complex の不等価比較
// 期待: exit=1
int main(void) {
    _Complex double a = 3.0;
    _Complex double b = 4.0;
    return a != b;
}
