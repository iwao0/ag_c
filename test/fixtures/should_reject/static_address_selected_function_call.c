static int values[2];

static int *return_values(void) {
  return values;
}

static int *selected_call =
    0 ? &values[0] : return_values();

int main(void) {
  return selected_call == values;
}
