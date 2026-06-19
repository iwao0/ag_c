// 2 重 for ループの試行回数 (3*4=12)
// 期待: exit=12
#include <assert.h>
int main(void) {
    int i = 0;
    int j = 0;
    int count = 0;
    for (i = 0; i < 3; i = i + 1)
        for (j = 0; j < 4; j = j + 1)
            count = count + 1;
    assert(count == 12);
    return 0;
}
