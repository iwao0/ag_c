// struct ポインタの -> 演算
// 1 + 2*10 + 3*100 = 321 mod 256 = 65
// 期待: exit=65
int main(void) {
    struct S { int x; int y; int z; };
    struct S s = {1, 2, 3};
    struct S *p = &s;
    return p->x + p->y * 10 + p->z * 100;
}
