// 多数の int 引数 (x0-x7 の境界を越える)
int sum10(int a, int b, int c, int d, int e,
          int f, int g, int h, int i, int j) {
  return a+b+c+d+e+f+g+h+i+j;
}
int main(void) {
  return sum10(1, 2, 3, 4, 5, 6, 7, 8, 9, 10);  // 55
}
// 期待: 55
