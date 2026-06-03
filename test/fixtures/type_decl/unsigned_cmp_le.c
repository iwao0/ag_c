// unsigned int の <= 比較
// 期待: exit=1
int main(void) {
    unsigned int a = (1u << 31) | 1;
    unsigned int b = a;
    return (a <= b) ? 1 : 0;
}
