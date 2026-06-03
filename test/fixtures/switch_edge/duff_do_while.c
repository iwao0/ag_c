// switch + do-while (Duff's device 風)
// case 1 で do に入り、x=10, 続けて case 2 で x=20
// 期待: exit=20
int main(void) {
    int x = 0;
    switch (1) {
        case 1: do { x = 10; case 2: x = 20; } while (0);
    }
    return x;
}
