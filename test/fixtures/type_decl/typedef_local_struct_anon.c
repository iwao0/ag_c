// ローカル typedef + 匿名 struct
// 期待: exit=9
int main(void) {
    typedef struct { int y; } L;
    L l;
    l.y = 9;
    return l.y;
}
