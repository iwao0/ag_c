// 2 段ポインタの値比較 (*pp == &x)
// 期待: exit=1
int main(void) {
    int x = 5;
    int *p = &x;
    int **pp = &p;
    return *pp == &x;
}
