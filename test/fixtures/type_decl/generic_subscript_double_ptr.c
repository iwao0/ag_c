// double*[0] で double にマッチ
// 期待: exit=42
int main(void) {
    double a[1] = {1.0};
    double *p = a;
    return _Generic(p[0], double: 42, default: 99);
}
