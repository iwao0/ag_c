// ネストした struct のメンバアクセス
// 1 + 2*10 + 3*100 = 321、 mod 256 = 65
// 期待: exit=65
int main(void) {
    struct A { int x; struct B { int y; int z; } b; };
    struct A a;
    a.x = 1;
    a.b.y = 2;
    a.b.z = 3;
    return a.x + a.b.y * 10 + a.b.z * 100;
}
