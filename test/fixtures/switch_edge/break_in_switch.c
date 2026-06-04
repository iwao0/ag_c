// switch 内 break
// 期待: exit=7
int main(void) {
    int a = 1;
    int b = 0;
    switch (a) {
        case 1: b = 7; break;
        default: b = 9;
    }
    return b;
}
