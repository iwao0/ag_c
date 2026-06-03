// ~0xFF & 0xFF = 0
// 期待: exit=0
int main(void) {
    int x = 0xFF;
    return ~x & 0xFF;
}
