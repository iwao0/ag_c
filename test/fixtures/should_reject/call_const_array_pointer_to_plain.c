/* An argument conversion cannot discard a pointed-to array element qualifier. */
static int read(int (*pointer)[3]) {
  return (*pointer)[0];
}

int main(void) {
  const int values[3] = {0};
  return read(&values);
}
