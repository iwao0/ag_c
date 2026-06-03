// sizeof(struct S[3]) = 12 (= 4*3)
// 期待: exit=12
int main(void) {
    struct S { int x; };
    return sizeof(struct S[3]);
}
