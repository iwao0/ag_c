// int * volatile * マッチ
// 期待: exit=2
int main(void) {
    int x = 0;
    int *p = &x;
    int * volatile *pp = &p;
    return _Generic(pp, int**: 1, int * volatile *: 2, default: 3);
}
