// ローカル typedef + 匿名 union
// 期待: exit=4
int main(void) {
    typedef union { int y; } L;
    L l;
    l.y = 4;
    return l.y;
}
