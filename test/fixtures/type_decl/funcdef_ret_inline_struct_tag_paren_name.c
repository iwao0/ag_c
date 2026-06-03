// 関数名を () で囲んだ inline struct タグ戻り値
// 期待: exit=3
struct S { int x; } (f)(void) {
    struct S s;
    s.x = 3;
    return s;
}
int main(void) { return f().x; }
