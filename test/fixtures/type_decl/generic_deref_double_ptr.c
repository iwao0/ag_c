// double* の deref で double にマッチ
// 期待: exit=42
int main(void) {
    double d = 1.0;
    double *p = &d;
    return _Generic(*p, double: 42, default: 99);
}
