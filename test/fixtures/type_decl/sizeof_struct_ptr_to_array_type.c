// sizeof(struct S (*)[3]) = 8
// 期待: exit=8
int main(void) {
    struct S { int x; };
    return sizeof(struct S (*)[3]);
}
