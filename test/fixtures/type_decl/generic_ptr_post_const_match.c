// int const * マッチ (= const int *)
// 期待: exit=2
int main(void) {
    int x = 0;
    int const *p = &x;
    return _Generic(p, int const *: 2, int *: 1, default: 3);
}
