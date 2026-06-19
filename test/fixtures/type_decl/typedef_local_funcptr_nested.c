// ローカル typedef + 関数ポインタの冗長括弧
// 期待: exit=0
#include <assert.h>
int main(void) {
    typedef int (*(*fp_t))(int);
    fp_t p;
    return 0;
}
