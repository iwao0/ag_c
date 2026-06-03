// 匿名 struct メンバ (パース確認)
// 期待: exit=7
int main(void) {
    struct S { struct { int x; }; int y; };
    return 7;
}
