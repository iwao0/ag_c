// int** マッチ
// 期待: exit=2
int main(void) {
    int x = 0;
    char c = 0;
    int *pi = &x;
    char *pc = &c;
    int **ppi = &pi;
    return _Generic(ppi, char**: 1, int**: 2, default: 3);
}
