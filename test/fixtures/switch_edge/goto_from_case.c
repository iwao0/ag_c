// case から switch 外のラベルへ goto
// 期待: exit=42
int main(void) {
    int x = 1;
    switch (x) {
        case 1: goto done;
        case 2: return 20;
        default: return 99;
    }
done:
    return 42;
}
