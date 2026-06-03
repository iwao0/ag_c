// typedef union U U; (前方宣言)
// 期待: exit=8
typedef union U U;
union U { int x; };
int main(void) {
    U u;
    u.x = 8;
    return u.x;
}
