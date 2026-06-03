// 2 段ポインタ deref
// 期待: exit=42
int main(void) {
    int x = 42;
    int *p = &x;
    int **pp = &p;
    return **pp;
}
