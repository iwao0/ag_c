struct callback_value {
  int elements[3];
};

enum { nested_parameter = 7 };

static int read_callback_value(struct callback_value value) {
  return value.elements[0] + value.elements[2];
}

static int inspect_nested_prototype(
    int (*callback)(struct callback_value nested_parameter),
    int (*row)[sizeof(nested_parameter)]) {
  struct callback_value value = {{10, 20, 28}};
  return callback(value) +
         (int)(sizeof(*row) / sizeof((*row)[0]));
}

int main(void) {
  int row[sizeof(int)] = {0};
  return inspect_nested_prototype(read_callback_value, &row) != 42;
}
