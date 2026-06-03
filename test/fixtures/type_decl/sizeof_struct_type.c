// sizeof(struct S) = 4 (int 1 つ)
// 期待: exit=4
int main(void) {
    struct S { int x; };
    return sizeof(struct S);
}
