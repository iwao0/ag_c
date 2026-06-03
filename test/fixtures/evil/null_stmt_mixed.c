// 空文 + 通常文混在
// 期待: exit=3
int main(void) {
    int x = 1;
    ;
    x = x + 2;
    ; ;
    return x;
}
