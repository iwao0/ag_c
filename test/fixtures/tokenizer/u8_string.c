// u8"ABC" は UTF-8 char[]。s[0]='A'=65, s[2]='C'=67 → 132
// 期待: exit=132
int main(void) {
    char *s = u8"ABC";
    return s[0] + s[2];
}
