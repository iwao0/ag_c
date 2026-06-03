// typedef した unsigned int* がマッチ
// 期待: exit=2
typedef unsigned int *uip_t;
int main(void) {
    unsigned int u = 0;
    uip_t pu = &u;
    return _Generic(pu, int*: 1, unsigned int*: 2, default: 3);
}
