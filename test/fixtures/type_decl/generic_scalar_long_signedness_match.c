// long と unsigned long の区別
// 期待: exit=2
int main(void) {
    long l = 1;
    return _Generic(l, unsigned long: 1, long: 2, default: 3);
}
