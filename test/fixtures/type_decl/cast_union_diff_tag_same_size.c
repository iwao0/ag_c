// 同サイズ別タグ union 間 cast
// 期待: exit=9
int main(void) {
    union A { int x; };
    union B { int x; };
    union A a = {.x = 9};
    union B b = (union B)a;
    return b.x;
}
