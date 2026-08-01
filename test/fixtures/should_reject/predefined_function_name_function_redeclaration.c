/* A function declaration cannot use the reserved predefined function-name identifier. */
int __func__(void) {
  return 0;
}
