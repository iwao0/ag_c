// スカラから struct への cast (ag_c 拡張: 第1メンバに格納)
// 期待: exit=7
int main(void) {
    struct S { int x; int y; };
    return ((struct S)7).x;
}
