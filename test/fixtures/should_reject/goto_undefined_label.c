/* A goto statement must name a label defined in the same function. */
int main(void) {
  goto missing;
  return 0;
}
