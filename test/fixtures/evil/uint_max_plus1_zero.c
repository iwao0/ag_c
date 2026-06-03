// unsigned int のラップ (UINT_MAX+1 == 0)
// 期待: exit=1
int main(void) {
    unsigned int x = 4294967295u;
    x = x + 1;
    return x == 0;
}
