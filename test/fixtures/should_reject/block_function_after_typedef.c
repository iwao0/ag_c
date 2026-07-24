/* Typedef and function names share C's ordinary identifier namespace. */
int main(void) {
  typedef int helper;
  extern int helper(void);
  return 0;
}
