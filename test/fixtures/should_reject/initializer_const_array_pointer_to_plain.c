/* Initializing a pointer-to-array cannot discard the element const qualifier. */
int main(void) {
  const int values[3] = {0};
  int (*pointer)[3] = &values;
  return (*pointer)[0];
}
