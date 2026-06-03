// char バッファに数値を書き、添字で読む
// 期待: exit=3
int main(void) {
    char buf[3];
    buf[0] = 1;
    buf[1] = 2;
    buf[2] = 3;
    return buf[2];
}
