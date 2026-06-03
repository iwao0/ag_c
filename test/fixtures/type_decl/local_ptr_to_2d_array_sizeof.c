// 2D 配列へのポインタの sizeof(*p)
// 期待: exit=48 (3*4*4)
int main(void) {
    int (*p)[3][4];
    return sizeof(*p);
}
