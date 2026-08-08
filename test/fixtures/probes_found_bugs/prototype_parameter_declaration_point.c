enum { parameter_extent = 3 };

static int inspect_parameter_scope(
    int (*parameter_extent)[sizeof(parameter_extent)],
    int (*row)[sizeof(parameter_extent)]) {
  return (*parameter_extent)[3] + (*row)[sizeof(parameter_extent) - 1];
}

int main(void) {
  int values[sizeof(int)] = {10, 20, 30, 30};
  int row[sizeof(void *)] = {0};
  row[sizeof(row) / sizeof(row[0]) - 1] = 12;
  return inspect_parameter_scope(&values, &row) != 42;
}
