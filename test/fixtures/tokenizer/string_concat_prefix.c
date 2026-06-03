// `u8"AB" "CD"` の隣接連結 → "ABCD"。s[0]+s[3] = 65+68 = 133
// 期待: exit=133
int main(void) {
    char *s = u8"AB" "CD";
    return s[0] + s[3];
}
