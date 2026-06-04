// sizeof(funcall()) で double 戻り値関数なら 8
// 期待: exit=8
double half_pi(void) { return 1.57; }
int main(void) {
    return (int)sizeof(half_pi());
}
