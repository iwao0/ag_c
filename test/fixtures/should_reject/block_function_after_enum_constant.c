/* Enum constants and function names share the ordinary identifier namespace. */
int main(void) {
  enum { helper = 1 };
  extern int helper(void);
  return 0;
}
