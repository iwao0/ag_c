// switch の case マッチ
// 期待: exit=20
int main(void) {
    int a = 2;
    switch (a) {
        case 1: return 10;
        case 2: return 20;
        default: return 30;
    }
}
