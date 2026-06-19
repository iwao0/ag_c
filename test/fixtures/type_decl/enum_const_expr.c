// enum 定数の定数式
// A=1, B=A+2=3, C=(B*2)-1=5
// 期待: exit=5
#include <assert.h>
int main(void) {
    enum E { A = 1, B = A + 2, C = (B * 2) - 1 };
    assert(C == 5);
    return 0;
}
