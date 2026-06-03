// & + struct 複合リテラル + ->
// 期待: exit=3
int main(void) {
    struct S { int x; };
    return (&(struct S){3})->x;
}
