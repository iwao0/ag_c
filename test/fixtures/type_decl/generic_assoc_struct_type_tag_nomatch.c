// 異なる struct タグで _Generic マッチしない → default
// 期待: exit=2
int main(void) {
    struct S { int x; };
    struct T { int x; };
    return _Generic((struct S){1}, struct T: 1, default: 2);
}
