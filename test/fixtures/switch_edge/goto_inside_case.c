// case 内から同じ switch 内の別ラベルへ goto
// 期待: exit=30
int main(void) {
    int x = 2;
    switch (x) {
        case 1: return 10;
        case 2: goto skip; return 20;
skip:   return 30;
        default: return 99;
    }
}
