// 3 段ポインタの参照
// 期待: exit=42
int main(void) {
    int x = 42;
    int *p = &x;
    int **pp = &p;
    int ***ppp = &pp;
    return ***ppp;
}
