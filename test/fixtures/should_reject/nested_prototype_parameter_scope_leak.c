struct callback_value {
  int elements[3];
};

int invalid_nested_scope(
    int (*callback)(struct callback_value nested_parameter),
    int (*row)[sizeof(nested_parameter)]);

int main(void) {
  return 0;
}
