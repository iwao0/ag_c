static int first_value(int values[1]) {
  return values[0];
}

static int apply(
    int (*callback)(int values[*]), int *values) {
  return callback(values);
}

int main(void) {
  int values[1] = {42};
  return apply(first_value, values) != 42;
}
