// unsigned int の 0-1 ラップ (=0xFFFFFFFF)
// 期待: exit=2 (1+1)
int main(void) {
    unsigned int x = 0;
    x = x - 1;
    return (x > 0) + (x == 4294967295u);
}
