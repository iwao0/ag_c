// 不等価判定で else 側へ分岐
// 期待: exit=10
int main(void) {
    int a = 3;
    if (a != 3) return 5;
    else return 10;
}
