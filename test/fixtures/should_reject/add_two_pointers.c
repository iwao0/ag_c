int main(void) {
  int values[2];
  int *first = &values[0];
  int *second = &values[1];
  return (first + second) == 0;
}
