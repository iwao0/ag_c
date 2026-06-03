// case ラベルに enum 定数を含む式 (A*2 = 4)
// 期待: exit=44
main() {
    enum E { A = 2 };
    a = 4;
    switch (a) {
        case A*2: return 44;
        default: return 0;
    }
}
