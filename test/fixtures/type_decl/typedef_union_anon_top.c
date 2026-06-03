// typedef union { ... } U; (匿名)
// 期待: exit=6
typedef union { int x; } U;
int main(void) {
    U u;
    u.x = 6;
    return u.x;
}
