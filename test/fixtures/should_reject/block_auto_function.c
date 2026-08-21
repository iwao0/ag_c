/* A block-scope function declaration cannot use auto. */
int main(void) {
  auto int function(void);
  return 0;
}
