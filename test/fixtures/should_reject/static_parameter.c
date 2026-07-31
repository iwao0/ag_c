/* A parameter declaration cannot use the static storage class. */
int function(static int value) {
  return value;
}

int main(void) {
  return 0;
}
