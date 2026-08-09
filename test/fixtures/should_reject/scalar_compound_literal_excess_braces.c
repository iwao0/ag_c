/* A scalar compound literal cannot contain a nested initializer list. */
int main(void) {
  return (int){{7}};
}
