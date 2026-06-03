// (struct S*)p で NULL ポインタのキャスト
// 期待: exit=1
int main(void) {
    struct S { int x; };
    struct S *p = 0;
    return ((struct S*)p) == 0;
}
