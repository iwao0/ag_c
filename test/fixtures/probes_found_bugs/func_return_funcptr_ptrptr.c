int inc(int x) {
  return x + 1;
}

int (*fp)(int) = inc;

int (**getpp(void))(int) {
  return &fp;
}

int main(void) {
  int a = (*getpp())(20);
  int b = (**getpp())(30);
  return a == 21 && b == 31 ? 0 : 1;
}
