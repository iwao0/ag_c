// typedef した const int* がマッチ
// 期待: exit=2
typedef const int *cip_t;
int main(void) {
    int x = 0;
    cip_t p = &x;
    return _Generic(p, int*: 1, const int*: 2, default: 3);
}
