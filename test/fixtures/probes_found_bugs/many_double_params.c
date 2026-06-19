// >8 個の double 引数 (stack 渡し)
#include <assert.h>
double sum10d(double a, double b, double c, double d, double e,
              double f, double g, double h, double i, double j) {
  return a+b+c+d+e+f+g+h+i+j;
}
int main(void) {
  assert((int)sum10d(1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0) == 55); return 0;  // 55
}
// 期待: 55
