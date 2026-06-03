// _Generic で struct 型を assoc に書く (パース+選択)
// 期待: exit=1
int main(void) {
    struct S { int x; };
    return _Generic((struct S){1}, struct S: 1, default: 2);
}
