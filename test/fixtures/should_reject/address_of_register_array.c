int main(void) {
  register int values[2] = {1, 2};
  int (*pointer)[2] = &values;
  return (*pointer)[0];
}
