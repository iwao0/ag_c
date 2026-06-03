// typedef した volatile int* がマッチ
// 期待: exit=2
typedef volatile int *vip_t;
int main(void) {
    int x = 0;
    vip_t p = &x;
    return _Generic(p, volatile int*: 2, int*: 1, default: 3);
}
