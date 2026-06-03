// typedef による不完全配列型
// 期待: exit=1
typedef int A[];
int main(void) {
    A *p = 0;
    return p == 0;
}
