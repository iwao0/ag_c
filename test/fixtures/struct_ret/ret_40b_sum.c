// 40 byte struct を返す (大きめ indirect return)
// 期待: exit=55 (1+2+...+10)
struct Big10 { int a; int b; int c; int d; int e; int f; int g; int h; int i; int j; };
struct Big10 make10() {
    struct Big10 s = {1,2,3,4,5,6,7,8,9,10};
    return s;
}
int main(void) {
    struct Big10 r = make10();
    return r.a + r.b + r.c + r.d + r.e + r.f + r.g + r.h + r.i + r.j;
}
