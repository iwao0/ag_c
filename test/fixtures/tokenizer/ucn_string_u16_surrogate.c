// u"..." の UCN が BMP 外 (U+10000 以上) のとき UTF-16 サロゲートペアにエンコードする。
// 修正前: `v & 0xFFFF` で 16bit に切り詰めて U+1F600 が 0xF600 になっていた。
// 修正後: hi=0xD83D, lo=0xDE00 の 2 hword を出力する。
// また内部にゼロバイトを含むため __TEXT,__cstring ではなく __DATA,__const に出力。
// little endian で bytes は: 3D D8 00 DE 00 00
// 期待: s[3] = 0xDE = 222
int main(void) {
    char *s = (char*)u"\U0001F600";
    return (int)(unsigned char)s[3];
}
