// enum 定数で条件演算/論理演算
// A=1, B=(A<2)=1, C=((A==1)&&(B||0))=1, D=C?7:9=7
// 期待: exit=7
#include <assert.h>
int main(void) {
    enum E { A = 1, B = (A < 2), C = (A == 1) && (B || 0), D = C ? 7 : 9 };
    assert(D == 7);
    return 0;
}
