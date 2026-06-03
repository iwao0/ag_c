// 匿名 union メンバ (パース確認)
// 期待: exit=7
int main(void) {
    struct S { union { int x; char c; }; int y; };
    return 7;
}
