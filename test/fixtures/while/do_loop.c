// do-while で 5 回ループ
// 期待: exit=5
int main(void) {
    int a = 0;
    do a = a + 1;
    while (a < 5);
    return a;
}
