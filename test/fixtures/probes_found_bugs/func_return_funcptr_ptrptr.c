int inc(int x) {
  return x + 1;
}

int (*fp)(int) = inc;

int (**getpp(void))(int) {
  return &fp;
}

int main(void) {
  return (*getpp())(41) == 42 ? 0 : 1;
}
