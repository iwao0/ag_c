// char バッファの合計
// 期待: exit=6
int main(void) {
    char buf[3];
    buf[0] = 1;
    buf[1] = 2;
    buf[2] = 3;
    return buf[0] + buf[1] + buf[2];
}
