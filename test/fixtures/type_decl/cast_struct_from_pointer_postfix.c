// ポインタから struct への cast (拡張)、後置 .p で deref
// 期待: exit=3
int main(void) {
    struct S { int *p; int q; };
    int x = 3;
    return *((struct S)&x).p;
}
