/* A block-scope function declaration cannot use static. */
int main(void) {
  static int function(void);
  return 0;
}
