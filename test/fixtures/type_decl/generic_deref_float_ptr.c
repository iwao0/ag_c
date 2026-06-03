// float* の deref で float にマッチ
// 期待: exit=11
int main(void) {
    float f = 1.0f;
    float *p = &f;
    return _Generic(*p, float: 11, default: 99);
}
