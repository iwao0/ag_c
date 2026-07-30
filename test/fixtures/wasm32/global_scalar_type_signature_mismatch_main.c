extern unsigned int global_scalar_type_value;

int main(void) {
  return global_scalar_type_value == 42u ? 0 : 1;
}
