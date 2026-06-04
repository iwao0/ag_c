// for ループで 1..10 の総和
// 期待: exit=55
int main(void) {
    int a;
    int b = 0;
    for (a = 1; a <= 10; a = a + 1) b = b + a;
    return b;
}
