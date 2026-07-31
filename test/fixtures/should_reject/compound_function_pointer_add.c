/* Compound pointer addition cannot be applied to a function pointer. */
static int function(void) {
  return 1;
}

int main(void) {
  int (*pointer)(void) = function;
  pointer += 1;
  return pointer != 0;
}
