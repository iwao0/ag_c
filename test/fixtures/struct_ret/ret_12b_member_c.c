// 12 byte struct を返して 3 番目のメンバ c だけ取る
// 期待: exit=12 (10+2)
struct Triple { int a; int b; int c; };
struct Triple make(int x) {
    struct Triple t = {x, x+1, x+2};
    return t;
}
int main(void) {
    struct Triple r = make(10);
    return r.c;
}
