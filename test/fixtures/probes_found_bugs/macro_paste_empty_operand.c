// `A ## B` で空引数 (placemarker) が ## の片側にあるとき、## を無視して
// placemarker を削除する (C11 6.10.3.2p3)。空 B を落とさず `jim ## ;` と解釈すると
// E1030 になっていた。c-testsuite 00202 相当。
#include <assert.h>
#include <stdio.h>

#define P(A, B) A ## B ; bob
#define Q(A, B) A ## B+

int main(void) {
    int bob, jim = 21;
    bob = P(jim, ) *= 2;   /* jim ; bob *= 2 → bob=42, jim=21 */
    assert(bob == 42 && jim == 21);
    jim = 60 Q(+, )3;      /* 60 + + 3 → 63 */
    assert(jim == 63);
    return 0;
}
