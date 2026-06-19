// ネストした switch
// 外側 case 1 内側 case 2 → return 22
// 期待: exit=22
#include <assert.h>
int main(void) {
    int o = 1;
    int result = 0;
    switch (o) {
        case 1: {
            int in = 2;
            switch (in) {
                case 1: result = 11; break;
                case 2: result = 22; break;
                default: result = 33; break;
            }
            break;
        }
        case 2: result = 44; break;
        default: result = 55; break;
    }
    assert(result == 22);
    return 0;
}
