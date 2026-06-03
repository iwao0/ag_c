// _Atomic int の load/store
// 期待: exit=42 (10+32)
int main(void) {
    _Atomic int x = 10;
    int y = x + 32;
    return y;
}
