// ブロック内に case ラベルがあっても到達できる (extension)
// 期待: exit=20
#include <assert.h>
int main(void) {
    int result = 0;
    switch (2) {
        case 1: { case 2: result = 20; }
    }
    assert(result == 20);
    return 0;
}
