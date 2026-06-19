// char* のポインタ加算 (1 byte ストライド)
// 期待: exit=3
#include <assert.h>
int main(void) {
    char b[4];
    b[0]=1; b[1]=2; b[2]=3; b[3]=4;
    char *p = b;
    p = p + 2;
    assert(*p == 3);
    return 0;
}
