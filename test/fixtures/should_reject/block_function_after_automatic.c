/* A block function declaration cannot redeclare a same-scope automatic. */
int main(void) {
  int helper;
  extern int helper(void);
  return 0;
}
