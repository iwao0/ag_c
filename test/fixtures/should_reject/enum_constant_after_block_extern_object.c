int main(void) {
  extern int shared;
  enum { shared = 1 };
  return 0;
}
