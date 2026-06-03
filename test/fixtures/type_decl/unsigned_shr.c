// unsigned int >> 31 で最上位 bit が 1 になる
// 期待: exit=1
int main(void) {
    unsigned int a = 0x80000000;
    return (a >> 31);
}
