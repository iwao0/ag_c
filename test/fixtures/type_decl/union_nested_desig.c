// union のネスト指定 .a[1]=3
// 期待: exit=3
int main(void) {
    union U { int a[2]; int z; };
    union U u = {.a[1] = 3};
    return u.a[1];
}
