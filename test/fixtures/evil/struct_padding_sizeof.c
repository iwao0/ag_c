// 構造体パディング込み sizeof
// char a + padding + int b + char c + padding = 12 (4 byte alignment)
// 期待: exit=12
int main(void) {
    struct S { char a; int b; char c; };
    return sizeof(struct S);
}
