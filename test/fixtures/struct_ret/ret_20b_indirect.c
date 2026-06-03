// 20 byte struct を戻り値で返す (ARM64 ABI: indirect return via x8)
// 期待: exit=35 (5+6+7+8+9)
struct Big { int a; int b; int c; int d; int e; };
struct Big make_big(int v) {
    struct Big b = {v, v+1, v+2, v+3, v+4};
    return b;
}
int main(void) {
    struct Big r = make_big(5);
    return r.a + r.b + r.c + r.d + r.e;
}
