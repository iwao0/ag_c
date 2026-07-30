extern int global_array_bound_value[2];

int main(void) {
  return global_array_bound_value[1] == 42 ? 0 : 1;
}
