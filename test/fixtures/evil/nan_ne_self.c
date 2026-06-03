// NaN != NaN は真 (IEEE754)
// 期待: exit=1
int main(void) {
    double x = 0.0 / 0.0;
    return x != x;
}
