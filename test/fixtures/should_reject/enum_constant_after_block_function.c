int main(void) {
  extern int helper(void);
  enum { helper = 1 };
  return 0;
}
