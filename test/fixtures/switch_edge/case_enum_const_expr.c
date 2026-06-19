// case ラベルに enum 定数を含む式 (A*2 = 4)
// 期待: exit=44
#include <assert.h>
int main(void) {
    enum E { A = 2 };
    int a = 4;
    int result;
    switch (a) {
        case A*2: result = 44; break;
        default: result = 0; break;
    }
    assert(result == 44);
    return 0;
}
