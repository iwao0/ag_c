// 関数仮引数 `char *p` の p[i] (バイトストライド)
// 期待: exit=6 (1+2+3)
int sum3(char *p) { return p[0] + p[1] + p[2]; }
int main(void) {
    char a[3];
    a[0]=1; a[1]=2; a[2]=3;
    return sum3(a);
}
