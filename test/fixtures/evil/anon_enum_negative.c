// 無名 enum で負の開始値から連番
// A=-3, B=-2, C=-1, D=0
// 期待: exit=0
#include <assert.h>
int main(void) {
    enum { A = -3, B, C, D };
    assert(A == -3);
    assert(B == -2);
    assert(C == -1);
    assert(D == 0);
    return 0;
}
