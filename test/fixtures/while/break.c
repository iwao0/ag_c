// while 内の break
// 期待: exit=1
int main(void) {
    int a = 0;
    while (1) {
        a = a + 1;
        break;
    }
    return a;
}
