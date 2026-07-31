/* A function definition cannot use the auto storage class. */
auto int function(void) {
  return 0;
}

int main(void) {
  return function();
}
