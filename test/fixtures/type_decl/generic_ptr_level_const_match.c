// int * const * マッチ (2 段ポインタの const 区別)
// 期待: exit=2
int main(void) {
    int x = 0;
    int *p = &x;
    int * const *pp = &p;
    return _Generic(pp, int**: 1, int * const *: 2, default: 3);
}
