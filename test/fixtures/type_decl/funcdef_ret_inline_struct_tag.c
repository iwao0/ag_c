// 関数定義の戻り値型に inline struct タグを書く
// 期待: exit=3
struct S { int x; } f(void) {
    struct S s;
    s.x = 3;
    return s;
}
int main(void) { return f().x; }
