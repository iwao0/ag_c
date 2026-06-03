// sizeof のオペランドは評価されない (x=99 の代入は走らない)
// 期待: exit=0
int main(void) {
    int x = 0;
    sizeof(x = 99);
    return x;
}
