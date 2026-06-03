// 同型 union 同士の cast
// 期待: exit=9
int main(void) {
    union U { int x; char y; };
    union U u = (union U)(union U){.x = 9};
    return u.x;
}
