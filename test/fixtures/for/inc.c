// for ループの終了条件 (10 回回って a=10)
// 期待: exit=10
int main(void) {
    int a;
    for (a = 0; a < 10; a = a + 1) a;
    return a;
}
