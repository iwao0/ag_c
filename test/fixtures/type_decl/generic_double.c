// _Generic で double 選択
// 期待: exit=0
#include <assert.h>
int main(void) {
    assert(_Generic(1.0, float: 11, double: 33, default: 22) == 33);
    return 0;
}
