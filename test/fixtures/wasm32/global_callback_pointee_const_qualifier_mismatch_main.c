typedef int global_callback_pointee_const_qualifier_t(
    const int value[1]);

extern global_callback_pointee_const_qualifier_t
    *global_callback_pointee_const_qualifier;

int main(void) {
  int value = 42;
  return global_callback_pointee_const_qualifier(&value);
}
