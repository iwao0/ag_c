// for 宣言子の変数は for スコープ限定
// 期待: exit=99
int main(void) {
    int i = 99;
    for (int i = 0; i < 5; i = i + 1) {}
    return i;
}
