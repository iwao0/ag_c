// struct にビットフィールド + 通常メンバ (パース確認)
// 期待: exit=7
int main(void) {
    struct S { int x:3; int y; };
    return 7;
}
