// case ラベルに定数式 (1+2)
// 期待: exit=33
main() {
    a = 3;
    switch (a) {
        case 1+2: return 33;
        default: return 0;
    }
}
