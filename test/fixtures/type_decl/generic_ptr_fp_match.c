// _Generic で double* マッチ
// 期待: exit=2
int main(void) {
    double d = 1.0;
    double *pd = &d;
    return _Generic(pd, int*: 1, double*: 2, default: 3);
}
