// _Complex double の初期化と == 比較
// 期待: exit=1
int main(void) {
    _Complex double a = 3.0;
    _Complex double b = a;
    _Complex double c = 3.0;
    return b == c;
}
