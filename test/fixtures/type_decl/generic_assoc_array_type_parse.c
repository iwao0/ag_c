// 配列型 assoc は ポインタとマッチしない → default
// 期待: exit=2
int main(void) {
    int *p = 0;
    return _Generic(p, int[3]: 1, default: 2);
}
