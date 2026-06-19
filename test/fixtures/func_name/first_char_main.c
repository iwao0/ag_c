// __func__ で関数名の先頭文字を取得 ('main'[0]=='m')
#include <assert.h>
int main(void) {
    assert(__func__[0] == 'm');
    return 0;
}
