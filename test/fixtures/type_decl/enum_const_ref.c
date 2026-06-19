// enum 定数の参照と連番
// A=1, B=2, C=10 → 13
// 期待: exit=13
#include <assert.h>
int main(void) {
    enum E { A = 1, B, C = 10 };
    assert(A + B + C == 13);
    return 0;
}
