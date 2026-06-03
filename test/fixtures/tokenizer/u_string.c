// u"AB" は char16_t[] (2 byte per element)。s[0]='A'=65
// 期待: exit=65
int main(void) {
    short *s = u"AB";
    return s[0];
}
