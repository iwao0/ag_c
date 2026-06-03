// 関数ごとに __func__ が独立した文字列を返すこと
// fa[1]='a'=97, fb[1]='b'=98 → 98-97+41 = 42
// 期待: exit=42
int fa(void) { return (int)__func__[1]; }
int fb(void) { return (int)__func__[1]; }
int main(void) {
    return fb() - fa() + 41;
}
