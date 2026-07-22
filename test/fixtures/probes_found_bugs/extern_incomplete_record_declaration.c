// extern宣言だけなら、recordが翻訳単位内で完成しなくても定義を要求しない。
#include <assert.h>

struct ExternalOnly;
extern struct ExternalOnly external_only;

int main(void) {
    assert(1);
    return 0;
}
