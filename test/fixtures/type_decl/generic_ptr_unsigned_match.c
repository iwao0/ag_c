// unsigned int* マッチ
// 期待: exit=2
int main(void) {
    int x = 0;
    unsigned int u = 0;
    unsigned int *pu = &u;
    return _Generic(pu, int*: 1, unsigned int*: 2, default: 3);
}
