/* A return conversion cannot discard a pointed-to array element qualifier. */
static int (*get(const int (*pointer)[3]))[3] {
  return pointer;
}

int main(void) {
  const int values[3] = {0};
  return (*get(&values))[0];
}
