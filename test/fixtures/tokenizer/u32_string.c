// U"AB" は char32_t[] (4 byte per element)。*s='A'=65
// 期待: exit=65
int main(void) {
    int *s = U"AB";
    return *s;
}
