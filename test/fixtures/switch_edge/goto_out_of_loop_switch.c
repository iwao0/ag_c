// switch 内から goto で外側ループも脱出
// i=0: a=1、i=1: a=3、i=2: goto out → a=3
// 期待: exit=3
int main(void) {
    int a = 0;
    int i = 0;
    while (i < 5) {
        switch (i) {
            case 0: a = a + 1; break;
            case 1: a = a + 2; break;
            case 2: goto out;
            default: a = a + 10; break;
        }
        i = i + 1;
    }
out:
    return a;
}
