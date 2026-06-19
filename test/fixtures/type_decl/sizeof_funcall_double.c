// sizeof(funcall()) で double 戻り値関数なら 8
// 期待: exit=8
#include <assert.h>
double half_pi(void) { return 1.57; }
int main(void) {
    assert((int)sizeof(half_pi()) == 8);
    return 0;
}
