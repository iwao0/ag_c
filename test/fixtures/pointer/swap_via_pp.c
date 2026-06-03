// 2 段ポインタを使った swap
// swap 後 *px=20, *py=10 → 20+10*10=120
// 期待: exit=120
void swap(int **a, int **b) {
    int *t = *a;
    *a = *b;
    *b = t;
}
int main(void) {
    int x = 10;
    int y = 20;
    int *px = &x;
    int *py = &y;
    swap(&px, &py);
    return *px + *py * 10;
}
