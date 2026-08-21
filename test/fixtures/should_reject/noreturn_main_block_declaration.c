/* A block-scope declaration still declares the hosted main function. */
int helper(void) {
  _Noreturn int main(void);
  return 0;
}

int main(void) { return helper(); }
