/* A conditional expression cannot combine object and function pointers. */
static int function(void) {
  return 1;
}

int main(void) {
  int value = 1;
  return (1 ? &value : function) != 0;
}
