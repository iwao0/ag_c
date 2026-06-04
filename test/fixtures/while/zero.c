// while 条件が常に偽なら本体は実行されない
// 期待: exit=0
int main(void) {
    int a = 0;
    while (0) a = a + 1;
    return a;
}
