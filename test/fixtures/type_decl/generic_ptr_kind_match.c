// _Generic で char* マッチ
// 期待: exit=0
#include <assert.h>
int main(void) {
    int x = 0;
    char c = 0;
    int *pi = &x;
    char *pc = &c;
    assert(_Generic(pc, int*: 1, char*: 2, default: 3) == 2);
    return 0;
}
