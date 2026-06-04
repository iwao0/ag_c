// if-else 本体にブロック (複数文 + return)
// 期待: exit=5
int main(void) {
    if (1) {
        int a = 2;
        int b = 3;
        return a + b;
    } else {
        return 0;
    }
}
