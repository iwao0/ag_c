// UCN > 0xFF (Hiragana あ = U+3042) を含む narrow char 文字列
// 修正前: コードポイントの下位 1 バイトに切り詰め、s[0]=0x42 になっていた
// 対応: gen_string_literals で codepoint を UTF-8 エンコード
// U+3042 → UTF-8: 0xE3 0x81 0x82 (3 byte)
// s[0]+s[1]+s[2] = 227+129+130 = 486 mod 256 = 230
// 期待: exit=230
int main(void) {
    char *s = "\u3042";
    return (int)((unsigned char)s[0] + (unsigned char)s[1] + (unsigned char)s[2]);
}
