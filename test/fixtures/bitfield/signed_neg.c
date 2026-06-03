// 符号付きビットフィールドへの -1 書き込みは負と判定される
// 期待: exit=42
int main(void) {
    struct S { int f:4; };
    struct S s;
    s.f = -1;
    return (s.f < 0) ? 42 : 0;
}
