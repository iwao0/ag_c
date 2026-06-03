// const int* と int* の区別
// 期待: exit=2
int main(void) {
    int x = 0;
    const int *p = &x;
    return _Generic(p, int*: 1, const int*: 2, default: 3);
}
