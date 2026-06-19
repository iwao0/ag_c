// for + switch + continue: i%3 で動作分岐
// case 0/1 で continue、case 2 で break → s+=1000
// 計算: i=0..9 で sum 計算後 %256 = 6
// 期待: exit=6
#include <assert.h>
int main(void) {
    int i = 0;
    int s = 0;
    for (i = 0; i < 10; i = i + 1) {
        switch (i % 3) {
            case 0: s = s + 1; continue;
            case 1: s = s + 10; continue;
            case 2: s = s + 100; break;
        }
        s = s + 1000;
    }
    assert(s % 256 == 6);
    return 0;
}
