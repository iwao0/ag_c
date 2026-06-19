// switch 内 continue は外側ループを継続する
// i=1: s+=1、i=2: continue、i=3: s+=3、i=4: s+=4 → s=8
// 期待: exit=8
#include <assert.h>
int main(void) {
    int i = 0;
    int s = 0;
    while (i < 4) {
        i = i + 1;
        switch (i) {
            case 2: continue;
            default: s = s + i;
        }
    }
    assert(s == 8);
    return 0;
}
