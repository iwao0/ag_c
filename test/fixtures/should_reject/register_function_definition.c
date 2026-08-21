/* A function definition cannot use the register storage class. */
register int function(void) {
  return 0;
}

int main(void) {
  return function();
}
