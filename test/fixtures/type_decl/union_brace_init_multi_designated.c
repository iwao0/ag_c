// union に複数指定 (最後の y=2 が有効、他は上書き)
// 期待: exit=2
int main(void) {
    union U { int x; char y; };
    union U u = {.x = 7, .y = 2};
    return u.y;
}
