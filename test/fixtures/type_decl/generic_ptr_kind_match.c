// _Generic で char* マッチ
// 期待: exit=2
int main(void) {
    int x = 0;
    char c = 0;
    int *pi = &x;
    char *pc = &c;
    return _Generic(pc, int*: 1, char*: 2, default: 3);
}
