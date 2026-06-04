// 等価判定で if 側へ分岐
// 期待: exit=5
int main(void) {
    int a = 3;
    if (a == 3) return 5;
    else return 10;
}
