// signed int オーバーフロー (INT_MAX+1 が負になる)
// 期待: exit=1
int main(void) {
    int x = 2147483647;
    x = x + 1;
    return x < 0;
}
