// 関数仮引数 `short *p` の p[i] (2 byte ストライド)
// 期待: exit=15 (4+5+6)
int sum3(short *p) { return p[0] + p[1] + p[2]; }
int main(void) {
    short a[3];
    a[0]=4; a[1]=5; a[2]=6;
    return sum3(a);
}
