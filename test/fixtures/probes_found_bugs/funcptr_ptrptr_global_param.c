int inc(int x) {
  return x + 1;
}

int (*global_fp)(int) = inc;
int (**global_pp)(int) = &global_fp;

int apply(int (**pp)(int), int x) {
  return (*pp)(x);
}

int main(void) {
  int a = (*global_pp)(41);
  int b = apply(global_pp, 50);
  return (a == 42 && b == 51) ? 0 : 1;
}
