// do-while + break
// 期待: exit=3
int main(void) {
    int x = 0;
    do {
        x = x + 1;
        if (x == 3) break;
    } while (x < 10);
    return x;
}
