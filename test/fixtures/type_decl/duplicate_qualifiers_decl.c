// 同じ修飾子を重複してもパースを通すこと
// 8+9+1 = 18
// 期待: exit=18
int main(void) {
    const const int x = 8;
    volatile volatile int y = 9;
    int *restrict restrict p = 0;
    return x + y + (p == 0);
}
