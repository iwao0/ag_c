// 幅 3 の unsigned に 9 (=0b1001) を書くと下位 3 bit のみ残る (=0b001=1)
// 期待: exit=1
int main(void) {
    struct S { unsigned int f:3; };
    struct S s;
    s.f = 9;
    return s.f;
}
