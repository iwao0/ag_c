// __func__ は呼び出された関数自身の名前を指すこと ('h'=='helper'[0]=104)
// 期待: exit=104
int helper(void) {
    return (int)__func__[0];
}
int main(void) {
    return helper();
}
