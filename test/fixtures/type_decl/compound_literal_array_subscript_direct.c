// (T[N]){...}[i] のような直接添字
// 期待: exit=0
#include <assert.h>
int main(void) {
    int v = (int[3]){7, 8, 9}[2];
    assert(v == 9);
    return 0;
}
