// 複合リテラル (int){3}
// 期待: exit=0
#include <assert.h>
int main(void) {
    assert((int){3} == 3);
    return 0;
}
