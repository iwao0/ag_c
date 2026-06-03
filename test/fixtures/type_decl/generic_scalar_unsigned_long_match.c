// unsigned long と unsigned int の区別
// 期待: exit=2
int main(void) {
    unsigned long ul = 1;
    return _Generic(ul, unsigned long: 2, unsigned int: 1, default: 3);
}
