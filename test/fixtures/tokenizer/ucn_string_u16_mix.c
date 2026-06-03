// u"..." で BMP 内と BMP 外 (サロゲートペア) を混在させる。
// "A\U0001F600B" は UTF-16 で:
//   A=0x0041, U+1F600 → 0xD83D 0xDE00 (サロゲートペア), B=0x0042, 終端 0x0000
// little endian bytes: 41 00 3D D8 00 DE 42 00 00 00
// s[0]=0x41, s[2]=0x3D, s[3]=0xD8, s[5]=0xDE, s[6]=0x42
// 期待: exit=99 (全て一致)
int main(void) {
    char *s = (char*)u"A\U0001F600B";
    if ((unsigned char)s[0] != 0x41) return 1;
    if ((unsigned char)s[2] != 0x3D) return 2;
    if ((unsigned char)s[3] != 0xD8) return 3;
    if ((unsigned char)s[5] != 0xDE) return 4;
    if ((unsigned char)s[6] != 0x42) return 5;
    return 99;
}
