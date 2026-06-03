// switch 内 break
// 期待: exit=7
main() {
    a = 1;
    b = 0;
    switch (a) {
        case 1: b = 7; break;
        default: b = 9;
    }
    return b;
}
