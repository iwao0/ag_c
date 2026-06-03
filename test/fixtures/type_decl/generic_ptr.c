// _Generic で int* 選択
// 期待: exit=3
int main(void) {
    int *p = 0;
    return _Generic(p, int*: 3, default: 7);
}
