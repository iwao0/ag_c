/* A string literal can initialize an array but cannot be assigned to one. */
int main(void) {
  char values[4] = {0};
  values = "abc";
  return values[0];
}
