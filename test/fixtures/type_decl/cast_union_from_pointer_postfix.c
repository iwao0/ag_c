// ポインタから union への cast + メンバ比較
// 期待: exit=1
int main(void) {
    union U { int *p; int q; };
    int x = 3;
    return ((union U)&x).p == &x;
}
