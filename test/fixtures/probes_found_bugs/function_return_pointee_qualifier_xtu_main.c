const volatile int *function_return_pointee_qualifier(void);

int main(void) {
  return *function_return_pointee_qualifier();
}
