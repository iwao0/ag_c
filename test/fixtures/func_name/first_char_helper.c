// __func__ は呼び出された関数自身の名前を指すこと ('helper'[0]=='h')
#include <assert.h>
int helper(void) {
    return (int)__func__[0];
}
int main(void) {
    assert(helper() == 'h');
    return 0;
}
